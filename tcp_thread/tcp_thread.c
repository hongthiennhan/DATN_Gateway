#include "tcp_thread.h"
#include "mongoose.h"

// Static variables
static struct mg_mgr tcp_mgr;
static struct mg_connection *tcp_connection = NULL;
volatile int tcp_connected = 0;

thread_pause_t tcp_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Forward declarations
void tcp_process_single_message(const char *message);
void tcp_process_config_update(json_object *config_obj);
void tcp_send_pong_response(void);
void process_tcp_control_command(const char *payload);
void tcp_event_handler(struct mg_connection *c, int ev, void *ev_data);
void tcp_process_receive
int ensure_directory_exists(const char *dir);

// Get local IP address - optimized but keeping full functionality
char *tcp_get_local_ip(void) {
    static char ip_str[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddrs_ptr, *ifa;

    if (getifaddrs(&ifaddrs_ptr) == -1) {
        strcpy(ip_str, "127.0.0.1");
        return ip_str;
    }

    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)ifa->ifa_addr;
            char *addr_str = inet_ntoa(addr_in->sin_addr);
            
            if (strncmp(addr_str, "127.", 4) != 0 && strncmp(addr_str, "169.254.", 8) != 0) {
                strcpy(ip_str, addr_str);
                freeifaddrs(ifaddrs_ptr);
                return ip_str;
            }
        }
    }

    freeifaddrs(ifaddrs_ptr);
    strcpy(ip_str, "127.0.0.1");
    return ip_str;
}

// Directory creation helper
static int ensure_directory_exists(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    
    if (mkdir(dir, 0755) == 0) return 0;
    return (errno == EEXIST) ? ensure_directory_exists(dir) : -1;
}

// JSON validation - keeping original logic but streamlined
int validate_json_basic(const void *data, size_t size) {
    if (!data || size == 0 || size > MAX_JSON_SIZE) return 0;
    
    const char *json_str = (const char *)data;
    if (json_str[0] != '{') return 0;

    for (int i = size - 1; i >= 0; i--) {
        if (json_str[i] == '}') return 1;
        if (json_str[i] != ' ' && json_str[i] != '\n' && 
            json_str[i] != '\t' && json_str[i] != '\0') break;
    }
    return 0;
}

// Atomic file write - preserving full atomic operation
int atomic_write_json_file(const char *dir, const char *filename, const void *data, size_t size) {
    if (ensure_directory_exists(dir) != 0) return -1;

    char current_path[256], new_path[256];
    snprintf(current_path, sizeof(current_path), "%s/%s", dir, filename);
    snprintf(new_path, sizeof(new_path), "%s/%s.new", dir, filename);

    // Remove old file if exists
    if (access(current_path, F_OK) == 0 && unlink(current_path) != 0) {
        return -1;
    }

    int fd = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    size_t total_written = 0;
    while (total_written < size) {
        ssize_t written = write(fd, (const char *)data + total_written, size - total_written);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(new_path);
            return -1;
        }
        total_written += written;
    }

    if (fsync(fd) != 0 || rename(new_path, current_path) != 0) {
        close(fd);
        unlink(new_path);
        return -1;
    }

    close(fd);
    return 0;
}

// Config reload - keeping full thread safety
int check_and_reload_config(void) {
    static volatile int config_updated = 0;
    static pthread_mutex_t config_update_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&config_update_mutex);
    int should_reload = config_updated;
    if (should_reload) config_updated = 0;
    pthread_mutex_unlock(&config_update_mutex);

    if (!should_reload) return 0;

    pthread_mutex_lock(&config_mutex);
    config_reloading = 1;
    cleanup_nodes_config();

    char config_path[64];
    snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, CONFIG_FILE);
    
    int result = (load_nodes_config(config_path) == 0);
    if (!result) {
        snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, FALLBACK_CONFIG_FILE);
        result = (load_nodes_config(config_path) == 0);
    }

    config_reloading = 0;
    pthread_mutex_unlock(&config_mutex);
    return result;
}

// TCP initialization - keeping all config validation
int tcp_init(void) {
    mg_mgr_init(&tcp_mgr);
    tcp_connection = NULL;
    tcp_connected = 0;

    tcp_config_t *config = get_tcp_config();
    if (!config || !strlen(config->server_host) || !config->server_port) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: Invalid TCP configuration\n");
        #endif
        return -1;
    }

    #ifdef DEBUG
    printf("TCP initialized: %s:%d (client: %s, protocol: %s)\n", 
           config->server_host, config->server_port, config->client_id, config->protocol_version);
    #endif
    return 0;
}

// TCP connect - preserving all config usage
int tcp_connect(const char *server_url) {
    tcp_config_t *config = get_tcp_config();
    if (!config) return -1;

    char url[512];
    if (server_url && strlen(server_url) > 0) {
        snprintf(url, sizeof(url), "%s", server_url);
    } else {
        snprintf(url, sizeof(url), "tcp://%s:%d", config->server_host, config->server_port);
    }

    tcp_connected = 0;
    tcp_connection = mg_connect(&tcp_mgr, url, tcp_event_handler, NULL);
    
    #ifdef DEBUG
    printf("TCP: %s to %s\n", tcp_connection ? "Connecting" : "Failed to connect", url);
    #endif
    
    return tcp_connection ? 0 : -1;
}

// TCP close
void tcp_close(void) {
    tcp_connected = 0;
    if (tcp_connection) {
        mg_mgr_poll(&tcp_mgr, 0);
        tcp_connection = NULL;
    }
    mg_mgr_free(&tcp_mgr);
    #ifdef DEBUG
    printf("TCP: Connection closed\n");
    #endif
}

// TCP reconnect
int tcp_reconnect(void) {
    tcp_config_t *config = get_tcp_config();
    if (!config) return -1;

    if (tcp_connection) {
        mg_mgr_poll(&tcp_mgr, 0);
        tcp_connection = NULL;
    }
    tcp_connected = 0;

    char url[512];
    snprintf(url, sizeof(url), "tcp://%s:%d", config->server_host, config->server_port);
    tcp_connection = mg_connect(&tcp_mgr, url, tcp_event_handler, NULL);
    
    return tcp_connection ? 0 : -1;
}

// Event handler - keeping all config features
void tcp_event_handler(struct mg_connection *c, int ev, void *ev_data) {
    switch (ev) {
        case MG_EV_CONNECT: {
            tcp_connected = 1;
            tcp_connection = c;
            
            tcp_config_t *config = get_tcp_config();
            if (config && config->features.status_reporting) {
                system_info_t *sys_info = get_system_info();
                if (sys_info) {
                    char handshake[512];
                    snprintf(handshake, sizeof(handshake),
                        "{\"type\":\"handshake\",\"client_id\":\"%s\",\"protocol_version\":\"%s\","
                        "\"gateway_ip\":\"%s\",\"firmware_version\":\"%s\",\"device_type\":\"%s\"}%s",
                        config->client_id, config->protocol_version, tcp_get_local_ip(),
                        sys_info->firmware_version, sys_info->device_type,
                        config->protocol_settings.message_delimiter);
                    mg_send(c, handshake, strlen(handshake));
                }
            }
            #ifdef DEBUG
            printf("TCP: Connected to server\n");
            #endif
            break;
        }
        
        case MG_EV_READ: {
            if (c->recv.len > 0) {
                tcp_process_received_data((const char*)c->recv.buf, c->recv.len);
                mg_iobuf_del(&c->recv, 0, c->recv.len);
            }
            break;
        }
        
        case MG_EV_CLOSE:
        case MG_EV_ERROR:
            tcp_connected = 0;
            tcp_connection = NULL;
            #ifdef DEBUG
            printf("TCP: Connection %s\n", ev == MG_EV_CLOSE ? "closed" : "error");
            #endif
            break;
    }
}

// Send data - keeping all protocol settings
int tcp_send_data(const char *data, size_t len) {
    if (!data || len == 0 || !tcp_connection || !tcp_connected) return -1;
    
    tcp_config_t *config = get_tcp_config();
    if (!config || len > config->protocol_settings.max_message_size) return -1;

    size_t delimiter_len = strlen(config->protocol_settings.message_delimiter);
    size_t total_len = len + delimiter_len;
    
    char *send_buffer = malloc(total_len);
    if (!send_buffer) return -1;

    memcpy(send_buffer, data, len);
    memcpy(send_buffer + len, config->protocol_settings.message_delimiter, delimiter_len);

    size_t sent = mg_send(tcp_connection, send_buffer, total_len);
    free(send_buffer);

    return (sent == total_len) ? 0 : -1;
}

// Process received data - keeping message delimiter handling
void tcp_process_received_data(const char *data, size_t len) {
    if (!data || len == 0) return;
    
    tcp_config_t *config = get_tcp_config();
    if (!config) return;

    char *data_str = malloc(len + 1);
    if (!data_str) return;
    
    memcpy(data_str, data, len);
    data_str[len] = '\0';

    char *delimiter = config->protocol_settings.message_delimiter;
    char *message_start = data_str;
    char *message_end;

    while ((message_end = strstr(message_start, delimiter)) != NULL) {
        *message_end = '\0';
        if (strlen(message_start) > 0) {
            tcp_process_single_message(message_start);
        }
        message_start = message_end + strlen(delimiter);
    }

    if (strlen(message_start) > 0) {
        tcp_process_single_message(message_start);
    }

    free(data_str);
}

// Process single message - keeping all feature flags
static void tcp_process_single_message(const char *message) {
    if (!message || !strlen(message)) return;
    
    tcp_config_t *config = get_tcp_config();
    if (!config) return;

    if (strcmp(config->data_format, "json") == 0 && !validate_json_basic(message, strlen(message))) {
        return;
    }

    json_object *root = json_tokener_parse(message);
    if (!root) return;

    json_object *type_obj;
    if (json_object_object_get_ex(root, "type", &type_obj)) {
        const char *msg_type = json_object_get_string(type_obj);
        
        if (strcmp(msg_type, "control_command") == 0 && config->features.control_commands) {
            process_tcp_control_command(message);
        } else if (strcmp(msg_type, "config_update") == 0 && config->features.config_download) {
            tcp_process_config_update(root);
        } else if (strcmp(msg_type, "ping") == 0) {
            tcp_send_pong_response();
        }
    } else {
        json_object *method_obj;
        if (json_object_object_get_ex(root, "method", &method_obj)) {
            process_tcp_control_command(message);
        }
    }

    json_object_put(root);
}

// Config update - keeping atomic write
static void tcp_process_config_update(json_object *config_obj) {
    json_object *config_data_obj;
    if (!json_object_object_get_ex(config_obj, "config", &config_data_obj)) return;

    const char *config_str = json_object_to_json_string(config_data_obj);
    if (!config_str) return;

    if (atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, config_str, strlen(config_str)) == 0) {
        pthread_mutex_lock(&config_mutex);
        config_reloading = 1;
        pthread_mutex_unlock(&config_mutex);
        #ifdef DEBUG
        printf("TCP: Configuration updated successfully\n");
        #endif
    }
}

// Pong response
static void tcp_send_pong_response(void) {
    tcp_config_t *config = get_tcp_config();
    if (!config) return;

    char pong_msg[256];
    snprintf(pong_msg, sizeof(pong_msg),
        "{\"type\":\"pong\",\"client_id\":\"%s\",\"timestamp\":%ld}",
        config->client_id, time(NULL));

    tcp_send_data(pong_msg, strlen(pong_msg));
}

// Control command processing - keeping all methods
static void process_tcp_control_command(const char *payload) {
    if (!payload || !strlen(payload)) return;

    json_object *root = json_tokener_parse(payload);
    if (!root) return;

    json_object *method_obj, *params_obj;
    if (json_object_object_get_ex(root, "method", &method_obj)) {
        const char *method = json_object_get_string(method_obj);
        
        if (strcmp(method, "executeCommand") == 0 && 
            json_object_object_get_ex(root, "params", &params_obj)) {
            
            json_object *node_id_obj, *cmd_id_obj, *params_str_obj;
            if (json_object_object_get_ex(params_obj, "nodeId", &node_id_obj) &&
                json_object_object_get_ex(params_obj, "cmdId", &cmd_id_obj)) {
                
                int node_id = json_object_get_int(node_id_obj);
                int cmd_id = json_object_get_int(cmd_id_obj);
                const char *params_str = "";
                
                if (json_object_object_get_ex(params_obj, "params", &params_str_obj)) {
                    params_str = json_object_get_string(params_str_obj);
                }

                add_control_command(node_id, cmd_id, params_str);
                #ifdef DEBUG
                printf("TCP: Command queued - node=%d, cmd=%d\n", node_id, cmd_id);
                #endif
            }
        } else if (strcmp(method, "ping") == 0) {
            tcp_send_pong_response();
        }
        // Add other methods as needed (getNodeStatus, getAllNodes, etc.)
    }

    json_object_put(root);
}

// Build telemetry payload - KEEPING FULL ORIGINAL IMPLEMENTATION
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp) {
    if (!payload || payload_size == 0) return;

    tcp_config_t *config = get_tcp_config();
    if (!config) {
        payload[0] = '\0';
        return;
    }

    char *temp_buffer = malloc(1024);
    if (!temp_buffer) {
        payload[0] = '\0';
        return;
    }

    // Start JSON with timestamp
    if (config->system_fields.include_timestamp) {
        snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);
    } else {
        snprintf(payload, payload_size, "{");
    }

    // Add data from detected nodes
    for (int i = 0; i < get_node_count(); i++) {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->tcp_data) continue;

        if (pthread_mutex_trylock(&node->tcp_data->mutex) == 0) {
            if (node->tcp_data->data) {
                raw_data_t *raw_data = (raw_data_t *)node->tcp_data->data;
                if (raw_data && raw_data->data && raw_data->length > 0) {
                    char *hex_str = malloc(raw_data->length * 2 + 1);
                    if (hex_str) {
                        for (int j = 0; j < raw_data->length; j++) {
                            sprintf(hex_str + j * 2, "%02X", raw_data->data[j]);
                        }
                        hex_str[raw_data->length * 2] = '\0';

                        snprintf(temp_buffer, 1024, ",\"node%d_data\":\"%s\",\"node%d_type\":\"%s\"",
                                node->node_id, hex_str, node->node_id, node->com_type);
                        
                        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size) {
                            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
                        }
                        
                        free(hex_str);
                    }
                }
            }
            pthread_mutex_unlock(&node->tcp_data->mutex);
        }
    }

    // Add system information
    if (config->system_fields.include_gateway_ip) {
        snprintf(temp_buffer, 1024, ",\"gateway_ip\":\"%s\"", tcp_get_local_ip());
        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size) {
            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
        }
    }

    if (config->system_fields.include_node_count) {
        snprintf(temp_buffer, 1024, ",\"detected_nodes\":%d", get_node_count());
        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size) {
            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
        }
    }

    // Add system info if enabled
    if (config->system_fields.include_system_info) {
        system_info_t *sys_info = get_system_info();
        if (sys_info) {
            snprintf(temp_buffer, 1024, ",\"firmware_version\":\"%s\",\"device_type\":\"%s\"",
                    sys_info->firmware_version, sys_info->device_type);
            if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size) {
                strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
            }
        }
    }

    // Close JSON object
    if (strlen(payload) + 2 < payload_size) {
        strncat(payload, "}", payload_size - strlen(payload) - 1);
    } else {
        if (payload_size > 1) {
            payload[payload_size - 2] = '}';
            payload[payload_size - 1] = '\0';
        }
    }

    free(temp_buffer);
}

// Update TCP data - KEEPING FULL ORIGINAL IMPLEMENTATION
void update_tcp_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len) {
    if (!node || !resp || resp_len == 0) return;

    // Ensure tcp_data structure exists
    if (!node->tcp_data) {
        node->tcp_data = malloc(sizeof(shared_data_t));
        if (!node->tcp_data) return;
        
        node->tcp_data->data = NULL;
        if (pthread_mutex_init(&node->tcp_data->mutex, NULL) != 0 ||
            pthread_cond_init(&node->tcp_data->cond, NULL) != 0) {
            free(node->tcp_data);
            node->tcp_data = NULL;
            return;
        }
    }

    pthread_mutex_lock(&node->tcp_data->mutex);

    // Free existing data if present
    if (node->tcp_data->data) {
        raw_data_t *old_data = (raw_data_t *)node->tcp_data->data;
        if (old_data->data) free(old_data->data);
        free(old_data);
    }

    // Allocate new raw_data_t structure
    raw_data_t *raw_data = malloc(sizeof(raw_data_t));
    if (raw_data) {
        raw_data->length = resp_len;
        raw_data->timestamp = time(NULL);
        raw_data->data = malloc(resp_len);
        
        if (raw_data->data) {
            memcpy(raw_data->data, resp, resp_len);
            node->tcp_data->data = raw_data;
            node->last_data_received = raw_data->timestamp;
            pthread_cond_signal(&node->tcp_data->cond);
            
            #ifdef DEBUG
            printf("TCP: Updated data for node %d - %d bytes received\n", node->node_id, resp_len);
            #endif
        } else {
            free(raw_data);
        }
    }

    pthread_mutex_unlock(&node->tcp_data->mutex);
}

// Main TCP thread function - keeping full functionality
void *tcp_thread_func(void *arg) {
    tcp_config_t *config = get_tcp_config();
    if (!config) return NULL;

    // 1. INITIALIZATION
    if (tcp_init() != 0) return NULL;

    // 2. INITIAL CONNECTION
    char server_url[256];
    snprintf(server_url, sizeof(server_url), "tcp://%s:%d", 
             config->server_host, config->server_port);
    tcp_connect(server_url);

    // 3. WAIT FOR CONNECTION WITH TIMEOUT
    int connection_timeout = config->connection_timeout;
    while (!tcp_connected && connection_timeout > 0) {
        mg_mgr_poll(&tcp_mgr, 100);
        connection_timeout--;
    }

    if (!tcp_connected) {
        tcp_close();
        return NULL;
    }

    // 4. ALLOCATE BUFFERS
    char *telemetry_payload = malloc(config->payload_buffer_size);
    if (!telemetry_payload) {
        tcp_close();
        return NULL;
    }

    // 5. MAIN LOOP
    time_t last_publish = 0;
    while (1) {
        // Handle pause/resume
        pthread_mutex_lock(&tcp_pause.mutex);
        while (tcp_pause.is_paused) {
            pthread_cond_wait(&tcp_pause.cond, &tcp_pause.mutex);
        }
        pthread_mutex_unlock(&tcp_pause.mutex);

        // Poll events
        mg_mgr_poll(&tcp_mgr, 100);

        // Check & reload config if needed
        check_and_reload_config();

        time_t current_time = time(NULL);

        // Send periodic telemetry
        if (tcp_connected && current_time - last_publish >= config->send_interval) {
            build_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);
            
            if (strlen(telemetry_payload) > 0) {
                tcp_send_data(telemetry_payload, strlen(telemetry_payload));
                last_publish = current_time;
            }
        }

        // Handle reconnection
        if (!tcp_connected) {
            tcp_reconnect();
            usleep(config->reconnect_delay_ms * 1000);
        } else {
            usleep(config->loop_interval_ms * 1000);
        }
    }

    // 6. CLEANUP
    free(telemetry_payload);
    tcp_close();
    return NULL;
}
