#include "thread_func.h"

// Config update tracking
static volatile int config_updated = 0;
static pthread_mutex_t config_update_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global MQTT variables
struct mosquitto *mqtt_client = NULL;
volatile int mqtt_connected = 0;

/**
 * Safe MQTT config access
 */
mqtt_config_t *safe_get_mqtt_config(void) {
    pthread_mutex_lock(&config_mutex);
    if (config_reloading) {
        pthread_mutex_unlock(&config_mutex);
        return NULL;
    }
    mqtt_config_t *config = get_mqtt_config();
    pthread_mutex_unlock(&config_mutex);
    return config;
}

/**
 * Get local IP address for gateway identification
 */
char *get_local_ip()
{
    static char ip_str[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddrs_ptr, *ifa;
    if (getifaddrs(&ifaddrs_ptr) == -1)
    {
        strcpy(ip_str, "127.0.0.1");
        return ip_str;
    }

    // Find first non-loopback IPv4 address
    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)ifa->ifa_addr;
            char *addr_str = inet_ntoa(addr_in->sin_addr);
            if (strncmp(addr_str, "127.", 4) != 0 && strncmp(addr_str, "169.254.", 8) != 0)
            {
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

/**
 * Ensure directory exists for config files
 */
static int ensure_directory_exists(const char *dir)
{
    struct stat st;
    if (stat(dir, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            return 0;
        }
        else
        {
#ifdef DEBUG
            fprintf(stderr, "ERROR: %s exists but is not a directory\n", dir);
#endif
            return -1;
        }
    }
    if (mkdir(dir, 0755) == 0)
    {
#ifdef DEBUG
        printf("Created directory: %s\n", dir);
#endif
        return 0;
    }
    if (errno == EEXIST)
    {
        return ensure_directory_exists(dir);
    }
#ifdef DEBUG
    fprintf(stderr, "ERROR: Cannot create directory %s: %s\n", dir, strerror(errno));
#endif
    return -1;
}

/**
 * Validate basic JSON structure
 */
static int validate_json_basic(const void *data, size_t size)
{
    if (!data || size == 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Empty JSON data\n");
#endif
        return 0;
    }
    if (size > MAX_JSON_SIZE)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: JSON too large: %zu bytes\n", size);
#endif
        return 0;
    }
    const char *json_str = (const char *)data;
    if (json_str[0] != '{')
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: JSON must start with '{'\n");
#endif
        return 0;
    }

    // Find closing brace
    int found_closing = 0;
    for (int i = size - 1; i >= 0; i--)
    {
        if (json_str[i] == '}')
        {
            found_closing = 1;
            break;
        }
        else if (json_str[i] != ' ' && json_str[i] != '\n' &&
                 json_str[i] != '\t' && json_str[i] != '\0')
        {
            break;
        }
    }
    if (!found_closing)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: JSON must end with '}'\n");
#endif
        return 0;
    }
#ifdef DEBUG
    printf("JSON validation passed: %zu bytes\n", size);
#endif
    return 1;
}

/**
 * Atomic file write with durability guarantees
 */
/**
 * New file management with backup system
 */
static int atomic_write_json_file(const char *dir, const char *filename, const void *data, size_t size)
{
    if (ensure_directory_exists(dir) != 0)
    {
        return -1;
    }
    char current_path[64], new_path[64], backup_path[64];
    snprintf(current_path, sizeof(current_path), "%s/%s", dir, filename);
    snprintf(new_path,    sizeof(new_path),    "%s/%s.new",  dir, filename);
    snprintf(backup_path, sizeof(backup_path), "%s/config_backup.json", dir);
    /* Delete old config file if it exists */
    if (access(current_path, F_OK) == 0)
    {
        if (unlink(current_path) != 0)
        {
#ifdef DEBUG
            fprintf(stderr, "ERROR: Cannot delete old config file %s: %s\n",
                    current_path, strerror(errno));
#endif
            return -1;
        }
#ifdef DEBUG
        printf("Deleted old config file: %s\n", current_path);
#endif
    }

    /* Create new file */
    int fd = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Cannot create new file %s: %s\n",
                new_path, strerror(errno));
#endif
        return -1;
    }

    /* Write data to new file with partial-write handling */
    size_t total_written = 0;
    while (total_written < size)
    {
        ssize_t written = write(fd,
                                (const char *)data + total_written,
                                size - total_written);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
#ifdef DEBUG
            fprintf(stderr, "ERROR: Write to new file failed: %s\n",
                    strerror(errno));
#endif
            close(fd);
            unlink(new_path);      /* Clean up failed new file */
            return -1;
        }
        total_written += written;
    }

    /* Ensure data is flushed to storage */
    if (fsync(fd) != 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: fsync new file failed: %s\n", strerror(errno));
#endif
        close(fd);
        unlink(new_path);          /* Clean up failed new file */
        return -1;
    }
    close(fd);

    /* Atomically rename new file to current filename */
    if (rename(new_path, current_path) != 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: rename new file failed: %s\n", strerror(errno));
#endif
        unlink(new_path);          /* Clean up failed new file */
        return -1;
    }
#ifdef DEBUG
    printf("New config file created successfully: %s\n", current_path);
#endif

    /* After saving new file, delete old backup (if any) */
    if (access(backup_path, F_OK) == 0)
    {
        if (unlink(backup_path) != 0)
        {
#ifdef DEBUG
            fprintf(stderr, "WARNING: Cannot delete old backup %s: %s\n",
                    backup_path, strerror(errno));
#endif
            /* Do not return – the main file is already saved */
        }
#ifdef DEBUG
        else
        {
            printf("Deleted old backup file: %s\n", backup_path);
        }
#endif
    }

    /* Create new backup from current file */
    int src_fd = open(current_path, O_RDONLY);   /* fixed: O_RDONLY */
    if (src_fd < 0)
    {
#ifdef DEBUG
        fprintf(stderr, "WARNING: Cannot open current file for backup: %s\n",
                strerror(errno));
#endif
        return 0;                  /* Main file OK, backup failed */
    }

    int backup_fd = open(backup_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (backup_fd < 0)
    {
#ifdef DEBUG
        fprintf(stderr, "WARNING: Cannot create backup file: %s\n",
                strerror(errno));
#endif
        close(src_fd);
        return 0;                  /* Main file OK, backup failed */
    }

    /* Copy data to backup */
    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0)
    {
        ssize_t bytes_written = 0;
        while (bytes_written < bytes_read)
        {
            ssize_t result = write(backup_fd,
                                   buffer + bytes_written,
                                   bytes_read - bytes_written);
            if (result < 0)
            {
                if (errno == EINTR)
                    continue;
#ifdef DEBUG
                fprintf(stderr, "WARNING: Backup write failed: %s\n",
                        strerror(errno));
#endif
                close(src_fd);
                close(backup_fd);
                unlink(backup_path);   /* Remove incomplete backup */
                return 0;              /* Main file OK */
            }
            bytes_written += result;
        }
    }

    /* Ensure backup is flushed to storage */
    fsync(backup_fd);
    close(src_fd);
    close(backup_fd);
#ifdef DEBUG
    printf("Backup created successfully: %s\n", backup_path);
#endif
    return 0;
}


/**
 * Process control command from server
 */
void process_control_command(const char *payload)
{
    json_object *root = json_tokener_parse(payload);
    if (!root)
    {
#ifdef DEBUG
        printf("ERROR: Invalid JSON in control command\n");
#endif
        return;
    }
    json_object *method_obj, *params_obj;
    if (!json_object_object_get_ex(root, "method", &method_obj))
    {
#ifdef DEBUG
        printf("ERROR: No method in control command\n");
#endif
        json_object_put(root);
        return;
    }
    const char *method = json_object_get_string(method_obj);
#ifdef DEBUG
    printf("Processing control method: %s\n", method);
#endif
    if (strcmp(method, "executeCommand") == 0)
    {
        if (json_object_object_get_ex(root, "params", &params_obj))
        {
            json_object *node_id_obj, *cmd_id_obj, *params_str_obj;
            if (json_object_object_get_ex(params_obj, "nodeId", &node_id_obj) &&
                json_object_object_get_ex(params_obj, "cmdId", &cmd_id_obj))
            {
                int node_id = json_object_get_int(node_id_obj);
                int cmd_id = json_object_get_int(cmd_id_obj);
                const char *params_str = "";
                if (json_object_object_get_ex(params_obj, "params", &params_str_obj))
                {
                    params_str = json_object_get_string(params_str_obj);
                }
#ifdef DEBUG
                printf("Server command: node=%d, cmd=%d, params=%s\n", node_id, cmd_id, params_str);
#endif
                // Queue command for execution
                if (add_control_command(node_id, cmd_id, params_str) == 0)
                {
#ifdef DEBUG
                    printf("Command queued successfully\n");
#endif
                }
                else
                {
#ifdef DEBUG
                    printf("Failed to queue command\n");
#endif
                }
            }
        }
    }
    json_object_put(root);
}

/**
 * MQTT connection callback
 */
void on_mqtt_connect(struct mosquitto *mosq, void *userdata, int result)
{
    if (result == 0)
    {
        mqtt_connected = 1;
#ifdef DEBUG
        printf("MQTT connected successfully\n");
#endif
        mqtt_config_t *config = get_mqtt_config();
        system_info_t *sys_info = get_system_info();
        if (!config || !sys_info)
            return;
        // Subscribe to control topic from server
        if (strlen(config->topic_control) > 0)
        {
            mosquitto_subscribe(mqtt_client, NULL, config->topic_control, config->qos);
#ifdef DEBUG
            printf("Subscribed to control topic: %s\n", config->topic_control);
#endif
        }
        // Subscribe to RPC requests
        mosquitto_subscribe(mqtt_client, NULL, "v1/devices/me/rpc/request/+", config->qos);
#ifdef DEBUG
        printf("Subscribed to RPC requests\n");
#endif
        // Publish device attributes
        char *attributes = malloc(config->attributes_buffer_size);
        if (attributes)
        {
            snprintf(attributes, config->attributes_buffer_size,
                     "{"
                     "\"gateway_ip\":\"%s\","
                     "\"firmware_version\":\"%s\","
                     "\"device_type\":\"%s\","
                     "\"manufacturer\":\"%s\","
                     "\"model\":\"%s\","
                     "\"node_count\":%d"
                     "}",
                     get_local_ip(),
                     sys_info->firmware_version,
                     sys_info->device_type,
                     sys_info->manufacturer,
                     sys_info->model,
                     get_node_count());
            mosquitto_publish(mqtt_client, NULL, config->topic_attributes,
                              strlen(attributes), attributes, config->qos, false);
            free(attributes);
        }
    }
    else
    {
        mqtt_connected = 0;
#ifdef DEBUG
        printf("MQTT connection failed: %d\n", result);
#endif
    }
}

/**
 * MQTT disconnect callback
 */
void on_mqtt_disconnect(struct mosquitto *mosq, void *userdata, int result)
{
    mqtt_connected = 0;
#ifdef DEBUG
    printf("MQTT disconnected\n");
#endif
}

/**
 * MQTT publish callback
 */
void on_mqtt_publish(struct mosquitto *mosq, void *userdata, int mid)
{
    // Message published successfully
}

/**
 * MQTT message received callback - handles both config updates and control commands
 */
void on_mqtt_message_robust(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message)
{
    if (!message || !message->topic)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Invalid MQTT message\n");
#endif
        return;
    }
    
#ifdef DEBUG
    printf("MQTT message on topic: %s\n", message->topic);
#endif

    // Thread-safe config access with timeout
    pthread_mutex_lock(&config_mutex);
    if (config_reloading) {
        pthread_mutex_unlock(&config_mutex);
        return; // Skip processing during reload
    }
    
    mqtt_config_t *config = get_mqtt_config();
    if (!config) {
        pthread_mutex_unlock(&config_mutex);
        return;
    }
    // Copy strings to local variables to avoid dangling pointers
    char topic_control[256] = {0};
    if (config->topic_control && strlen(config->topic_control) > 0) {
        strncpy(topic_control, config->topic_control, sizeof(topic_control) - 1);
    }
    
    pthread_mutex_unlock(&config_mutex);
    
    // Handle control commands from server
    if ((strlen(topic_control) > 0 && strstr(message->topic, topic_control)) || 
        strstr(message->topic, "v1/devices/me/rpc/request/"))
    {
#ifdef DEBUG
        printf("Processing control command\n");
#endif
        if (message->payload && message->payloadlen > 0)
        {
            char *payload_str = malloc(message->payloadlen + 1);
            if (payload_str)
            {
                memcpy(payload_str, message->payload, message->payloadlen);
                payload_str[message->payloadlen] = '\0';
                process_control_command(payload_str);
                free(payload_str);
            }
        }
        return;
    }

    // Handle config updates from ThingsBoard
    if (strstr(message->topic, "v1/devices/me/attributes/response/") ||
        strcmp(message->topic, "v1/devices/me/attributes") == 0)
    {
#ifdef DEBUG
        printf("Processing config update\n");
#endif
        const void *payload = message->payload;
        size_t payload_size = (size_t)message->payloadlen;
        if (!validate_json_basic(payload, payload_size))
        {
#ifdef DEBUG
            fprintf(stderr, "ERROR: JSON validation failed\n");
#endif
            return;
        }

        // Save config update
        int result = atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, payload, payload_size);
        if (result == 0)
        {
#ifdef DEBUG
            printf("Config updated from server\n");
#endif
            // Use trylock to deadlock
            if (pthread_mutex_trylock(&config_update_mutex) == 0) {
                config_updated = 1;
                pthread_mutex_unlock(&config_update_mutex);
            }
        }
    }
}


/**
 * Check and reload config if updated
 */
int check_and_reload_config(void)
{
    pthread_mutex_lock(&config_update_mutex);
    int should_reload = config_updated;
    if (should_reload)
    {
        config_updated = 0;
    }
    pthread_mutex_unlock(&config_update_mutex);
    
    if (!should_reload) return 0;
    
    pthread_mutex_lock(&config_mutex);
    config_reloading = 1;
    
#ifdef DEBUG
    printf("Reloading config from server update\n");
#endif
    
    cleanup_nodes_config();
    
    char config_path[64];
    snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, CONFIG_FILE);
    
    int result = 0;
    if (load_nodes_config(config_path) == 0)
    {
#ifdef DEBUG
        printf("Config reloaded successfully\n");
#endif
        result = 1;
    }
    else
    {
#ifdef DEBUG
        fprintf(stderr, "Failed to reload config, using fallback\n");
#endif
        // Try fallback configs
        if (load_nodes_config(config_path) != 0)
        {
            snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, FALLBACK_CONFIG_FILE);
            load_nodes_config(config_path);
        }
    }
    
    config_reloading = 0;
    pthread_mutex_unlock(&config_mutex);
    
    return result;
}


/**
 * Request config from ThingsBoard
 */
int request_config_json_robust(void)
{
    pthread_mutex_lock(&config_mutex);
    mqtt_config_t *cfg = get_mqtt_config();
    if (!cfg || !mqtt_client || !mqtt_connected)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Cannot request config - MQTT not ready\n");
#endif
        pthread_mutex_unlock(&config_mutex);
        return 0;
    }
#ifdef DEBUG
    printf("Requesting config from server\n");
#endif
    // Set message callback
    mosquitto_message_callback_set(mqtt_client, on_mqtt_message_robust);
    // Subscribe to config topics
    mosquitto_subscribe(mqtt_client, NULL, "v1/devices/me/attributes/response/+", cfg->qos);
    mosquitto_subscribe(mqtt_client, NULL, "v1/devices/me/attributes", cfg->qos);
    // Request config
    const char *request_payload = "{\"sharedKeys\":\"config\"}";
    int rc = mosquitto_publish(mqtt_client, NULL, "v1/devices/me/attributes/request/1",
                               strlen(request_payload), request_payload, cfg->qos, false);
    if (rc == MOSQ_ERR_SUCCESS)
    {
#ifdef DEBUG
        printf("Config request sent\n");
#endif
        pthread_mutex_unlock(&config_mutex);
        return 1;
    }
    else
    {
#ifdef DEBUG
        fprintf(stderr, "Failed to send config request\n");
#endif
        
        return 0;
    }
}

/**
 * Build telemetry payload with node data
 */
/**
 * Build telemetry payload with node data
 */
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp)
{
    mqtt_config_t *config = get_mqtt_config();
    if (!config)
        return;
        
    char *temp_buffer = malloc(1024);
    if (!temp_buffer)
        return;
        
    // Start JSON with timestamp
    if (config->system_fields.include_timestamp)
    {
        snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);
    }
    else
    {
        snprintf(payload, payload_size, "{");
    }

    // Add data from detected nodes - KHÔNG LOCK config_mutex nữa
    for (int i = 0; i < get_node_count(); i++)
    {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->mqtt_data || !node->detected)
            continue;
            
        // Chỉ trylock node mutex thôi
        if (pthread_mutex_trylock(&node->mqtt_data->mutex) == 0) {
            if (node->mqtt_data->data)
            {
                raw_data_t *raw_data = (raw_data_t *)node->mqtt_data->data;
                if (raw_data && raw_data->data && raw_data->length > 0)
                {
                    // Convert to hex string
                    char *hex_str = malloc(raw_data->length * 2 + 1);
                    if (hex_str)
                    {
                        for (int j = 0; j < raw_data->length; j++)
                        {
                            sprintf(hex_str + j * 2, "%02X", raw_data->data[j]);
                        }
                        hex_str[raw_data->length * 2] = '\0';
                        snprintf(temp_buffer, 1024, ",\"node%d_data\":\"%s\",\"node%d_type\":\"%s\"",
                               node->node_id, hex_str, node->node_id, node->com_type);
                        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
                        free(hex_str);
                    }
                }
            }
            pthread_mutex_unlock(&node->mqtt_data->mutex);
        }
        // Nếu trylock fail thì skip, đừng block
    }

    // Add system info
    if (config->system_fields.include_gateway_ip)
    {
        snprintf(temp_buffer, 1024, ",\"gateway_ip\":\"%s\"", get_local_ip());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }

    if (config->system_fields.include_node_count)
    {
        snprintf(temp_buffer, 1024, ",\"detected_nodes\":%d", get_node_count());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }

    strncat(payload, "}", payload_size - strlen(payload) - 1);
    free(temp_buffer);
}


/**
 * Main MQTT thread function
 */
void *mqtt_thread_func(void *arg)
{
    mqtt_config_t *config = get_mqtt_config();
    if (!config)
    {
#ifdef DEBUG
        printf("No MQTT config available\n");
#endif
        return NULL;
    }

    // Initialize MQTT
    mosquitto_lib_init();
    mqtt_client = mosquitto_new(config->client_id, true, NULL);
    if (!mqtt_client)
    {
#ifdef DEBUG
        printf("Failed to create MQTT client\n");
#endif
        return NULL;
    }

    // Set credentials and callbacks
    mosquitto_username_pw_set(mqtt_client, config->username, config->password);
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);
// Connect to broker
#ifdef DEBUG
    printf("Connecting to MQTT broker %s:%d\n", config->broker_host, config->broker_port);
#endif
    int rc = mosquitto_connect(mqtt_client, config->broker_host, config->broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS)
    {
#ifdef DEBUG
        printf("Failed to connect to MQTT broker\n");
#endif
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

    // Start network loop
    mosquitto_loop_start(mqtt_client);
    // Wait for connection
    int connection_timeout = config->connection_timeout;
    while (!mqtt_connected && connection_timeout > 0)
    {
        usleep(100 * 1000);
        connection_timeout--;
    }
    if (!mqtt_connected)
    {
#ifdef DEBUG
        printf("MQTT connection timeout\n");
#endif
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

// Request initial config
#ifdef DEBUG
    printf("Requesting initial config\n");
#endif
    if (request_config_json_robust())
    {
        sleep(3); // Wait for response
    }

    // Check for config updates
    check_and_reload_config();
    // Main loop - publish telemetry and handle config updates
    time_t last_publish = 0, last_status = 0;
    char *telemetry_payload = malloc(config->payload_buffer_size);
    char *status_payload = malloc(config->payload_buffer_size);
    if (!telemetry_payload || !status_payload)
    {
#ifdef DEBUG
        printf("Memory allocation failed\n");
#endif
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&mqtt_pause.mutex);
        while (mqtt_pause.is_paused) {
            pthread_cond_wait(&mqtt_pause.cond, &mqtt_pause.mutex);  // Sleep and wait for signal
        }
        pthread_mutex_unlock(&mqtt_pause.mutex);

        time_t current_time = time(NULL);
        
        // Check for config updates
        check_and_reload_config();
        
        // Publish telemetry data - dùng config ban đầu thay vì get mới
        if (current_time - last_publish >= config->publish_interval)
        {
            build_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);
            rc = mosquitto_publish(mqtt_client, NULL, config->topic_telemetry,
                                 strlen(telemetry_payload), telemetry_payload,
                                 config->qos, false);
            if (rc == MOSQ_ERR_SUCCESS)
            {
                last_publish = current_time;
            }
        }

        // Send status update
        if (strlen(config->topic_status) > 0 && (current_time - last_status) >= 60)
        {
            snprintf(status_payload, config->payload_buffer_size,
                    "{\"status\":\"online\",\"timestamp\":%ld000,\"detected_nodes\":%d}",
                    current_time, get_node_count());
            mosquitto_publish(mqtt_client, NULL, config->topic_status,
                            strlen(status_payload), status_payload, config->qos, false);
            last_status = current_time;
        }

        // Handle reconnection
        if (!mqtt_connected)
        {
            mosquitto_reconnect(mqtt_client);
            usleep(config->reconnect_delay_ms * 1000);
        }

        usleep(config->loop_interval_ms * 1000);
    }

    // Cleanup
    free(telemetry_payload);
    free(status_payload);
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mosquitto_lib_cleanup();
    return NULL;
}
