#include "udp_thread.h"
#include "mongoose.h"

// Static variables for UDP management
static struct mg_mgr udp_mgr;
static struct mg_connection *udp_connection = NULL;
volatile int udp_connected = 0;

thread_pause_t udp_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};

// Forward declarations
static void process_udp_control_command(const char *payload);
static void udp_event_handler(struct mg_connection *c, int ev, void *ev_data);

// Get local IP address for UDP binding and identification
char *udp_get_local_ip(void)
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

// Initialize UDP manager and validate configuration
static int udp_init(void)
{
    mg_mgr_init(&udp_mgr);
    udp_connection = NULL;
    udp_connected = 0;

    udp_config_t *config = get_udp_config();
    if (!config || !strlen(config->server_host) || !config->server_port)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Invalid UDP configuration\n");
#endif
        return -1;
    }

#ifdef DEBUG
    printf("UDP initialized: %s:%d (client: %s, protocol: %s)\n",
           config->server_host, config->server_port, config->client_id, config->protocol_version);
#endif
    return 0;
}

// Establish UDP connection to server
static int udp_connect(const char *server_url)
{
    udp_config_t *config = get_udp_config();
    if (!config)
        return -1;

    char url[256];
    if (server_url && strlen(server_url) > 0)
    {
        snprintf(url, sizeof(url), "%s", server_url);
    }
    else
    {
        snprintf(url, sizeof(url), "udp://%s:%d", config->server_host, config->server_port);
    }

    udp_connected = 0;
    udp_connection = mg_connect(&udp_mgr, url, udp_event_handler, NULL);

#ifdef DEBUG
    printf("UDP: %s to %s\n", udp_connection ? "Connecting" : "Failed to connect", url);
#endif
    return udp_connection ? 0 : -1;
}

// Close UDP connection and cleanup resources
static void udp_close(void)
{
    udp_connected = 0;
    if (udp_connection)
    {
        mg_mgr_poll(&udp_mgr, 0);
        udp_connection = NULL;
    }
    mg_mgr_free(&udp_mgr);

#ifdef DEBUG
    printf("UDP: Connection closed\n");
#endif
}

// Reconnect UDP connection after failure
static int udp_reconnect(void)
{
    udp_config_t *config = get_udp_config();
    if (!config)
        return -1;

    if (udp_connection)
    {
        mg_mgr_poll(&udp_mgr, 0);
        udp_connection = NULL;
    }

    udp_connected = 0;
    char url[256];
    snprintf(url, sizeof(url), "udp://%s:%d", config->server_host, config->server_port);
    udp_connection = mg_connect(&udp_mgr, url, udp_event_handler, NULL);

    return udp_connection ? 0 : -1;
}

// Send data over UDP connection with protocol settings
static int udp_send_data(const char *data, size_t len)
{
    if (!data || len == 0 || !udp_connection || !udp_connected)
        return -1;

    udp_config_t *config = get_udp_config();
    if (!config || len > config->protocol_settings.max_message_size)
        return -1;

    size_t delimiter_len = strlen(config->protocol_settings.message_delimiter);
    size_t total_len = len + delimiter_len;

    char *send_buffer = malloc(total_len);
    if (!send_buffer)
        return -1;

    memcpy(send_buffer, data, len);
    memcpy(send_buffer + len, config->protocol_settings.message_delimiter, delimiter_len);

    size_t sent = mg_send(udp_connection, send_buffer, total_len);
    free(send_buffer);

    return (sent == total_len) ? 0 : -1;
}

// Send pong response to server ping
void udp_send_pong_response(void)
{
    udp_config_t *config = get_udp_config();
    if (!config)
        return;

    char pong_msg[256];
    snprintf(pong_msg, sizeof(pong_msg),
             "{\"type\":\"pong\",\"client_id\":\"%s\",\"timestamp\":%ld}",
             config->client_id, time(NULL));

    udp_send_data(pong_msg, strlen(pong_msg));
}

// Process individual message received from server
static void udp_process_single_message(const char *message)
{
    if (!message || !strlen(message))
        return;

    udp_config_t *config = get_udp_config();
    if (!config)
        return;

    // Validate JSON format if required
    if (strcmp(config->data_format, "json") == 0 && !validate_json_basic(message, strlen(message)))
    {
        return;
    }

    json_object *root = json_tokener_parse(message);
    if (!root)
        return;

    json_object *type_obj;
    if (json_object_object_get_ex(root, "type", &type_obj))
    {
        const char *msg_type = json_object_get_string(type_obj);

        if (strcmp(msg_type, "control_command") == 0 && config->features.control_commands)
        {
            process_udp_control_command(message);
        }
        else if (strcmp(msg_type, "ping") == 0)
        {
            udp_send_pong_response();
        }
        else
        {
            // Check for method-based commands
            json_object *method_obj;
            if (json_object_object_get_ex(root, "method", &method_obj))
            {
                process_udp_control_command(message);
            }
        }
    }

    json_object_put(root);
}

// Process received UDP data with message delimiter handling
static void udp_process_received_data(const char *data, size_t len)
{
    if (!data || len == 0)
        return;

    udp_config_t *config = get_udp_config();
    if (!config)
        return;

    char *data_str = malloc(len + 1);
    if (!data_str)
        return;

    memcpy(data_str, data, len);
    data_str[len] = '\0';

    char *delimiter = config->protocol_settings.message_delimiter;
    char *message_start = data_str;
    char *message_end;

    // Split data by delimiter and process each message
    while ((message_end = strstr(message_start, delimiter)) != NULL)
    {
        *message_end = '\0';
        if (strlen(message_start) > 0)
        {
            udp_process_single_message(message_start);
        }
        message_start = message_end + strlen(delimiter);
    }

    // Process remaining data if any
    if (strlen(message_start) > 0)
    {
        udp_process_single_message(message_start);
    }

    free(data_str);
}

// UDP event handler for Mongoose events
void udp_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    switch (ev)
    {
    case MG_EV_CONNECT:
    {
        udp_connected = 1;
        udp_connection = c;

        udp_config_t *config = get_udp_config();
        if (config && config->features.status_reporting)
        {
            system_info_t *sys_info = get_system_info();
            if (sys_info)
            {
                char handshake[512];
                snprintf(handshake, sizeof(handshake),
                         "{\"client_id\":\"%s\",\"protocol_version\":\"%s\","
                         "\"gateway_ip\":\"%s\",\"firmware_version\":\"%s\",\"device_type\":\"%s\"}%s",
                         config->client_id, config->protocol_version, udp_get_local_ip(),
                         sys_info->firmware_version, sys_info->device_type,
                         config->protocol_settings.message_delimiter);

                mg_send(c, handshake, strlen(handshake));
            }
        }

#ifdef DEBUG
        printf("UDP: Connected to server\n");
#endif
        break;
    }

    case MG_EV_READ:
    {
        if (c->recv.len > 0)
        {
            udp_process_received_data((const char *)c->recv.buf, c->recv.len);
            mg_iobuf_del(&c->recv, 0, c->recv.len);
        }
        break;
    }

    case MG_EV_CLOSE:
    case MG_EV_ERROR:
        udp_connected = 0;
        udp_connection = NULL;
#ifdef DEBUG
        printf("UDP: Connection %s\n", ev == MG_EV_CLOSE ? "closed" : "error");
#endif
        break;
    }
}

// Process control commands received from server
static void process_udp_control_command(const char *payload)
{
    if (!payload || !strlen(payload))
        return;

    json_object *root = json_tokener_parse(payload);
    if (!root)
        return;

    json_object *method_obj, *params_obj;
    if (json_object_object_get_ex(root, "method", &method_obj))
    {
        const char *method = json_object_get_string(method_obj);

        if (strcmp(method, "executeCommand") == 0 &&
            json_object_object_get_ex(root, "params", &params_obj))
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

                add_control_command(node_id, cmd_id, params_str);

#ifdef DEBUG
                printf("UDP: Command queued - node=%d, cmd=%d\n", node_id, cmd_id);
#endif
            }
        }
        else if (strcmp(method, "ping") == 0)
        {
            udp_send_pong_response();
        }
    }

    json_object_put(root);
}

// Build telemetry payload with node data and system information
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp)
{
    if (!payload || payload_size == 0)
        return;

    udp_config_t *config = get_udp_config();
    if (!config)
    {
        payload[0] = '\0';
        return;
    }

    char *temp_buffer = malloc(1024);
    if (!temp_buffer)
    {
        payload[0] = '\0';
        return;
    }

    // Start JSON with timestamp if enabled
    if (config->system_fields.include_timestamp)
    {
        snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);
    }
    else
    {
        snprintf(payload, payload_size, "{");
    }

    // Add data from detected nodes
    for (int i = 0; i < get_node_count(); i++)
    {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->udp_data)
            continue;

        if (pthread_mutex_trylock(&node->udp_data->mutex) == 0)
        {
            if (node->udp_data->data)
            {
                raw_data_t *raw_data = (raw_data_t *)node->udp_data->data;
                if (raw_data && raw_data->data && raw_data->length > 0)
                {
                    // Convert raw data to hex string
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

                        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size)
                        {
                            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
                        }

                        free(hex_str);
                    }
                }
            }
            pthread_mutex_unlock(&node->udp_data->mutex);
        }
    }

    // Add system information if enabled
    if (config->system_fields.include_gateway_ip)
    {
        snprintf(temp_buffer, 1024, ",\"gateway_ip\":\"%s\"", udp_get_local_ip());
        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size)
        {
            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
        }
    }

    if (config->system_fields.include_node_count)
    {
        snprintf(temp_buffer, 1024, ",\"detected_nodes\":%d", get_node_count());
        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size)
        {
            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
        }
    }

    // Add system info if enabled
    if (config->system_fields.include_system_info)
    {
        system_info_t *sys_info = get_system_info();
        if (sys_info)
        {
            snprintf(temp_buffer, 1024, ",\"firmware_version\":\"%s\",\"device_type\":\"%s\"",
                     sys_info->firmware_version, sys_info->device_type);
            if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size)
            {
                strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
            }
        }
    }

    // Close JSON object
    if (strlen(payload) + 2 < payload_size)
    {
        strncat(payload, "}", payload_size - strlen(payload) - 1);
    }
    else
    {
        if (payload_size > 1)
        {
            payload[payload_size - 2] = '}';
            payload[payload_size - 1] = '\0';
        }
    }

    free(temp_buffer);
}

// Update UDP data structure with response from nodes
void update_udp_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len)
{
    if (!node || !resp || resp_len == 0)
        return;

    // Ensure udp_data structure exists
    if (!node->udp_data)
    {
        node->udp_data = malloc(sizeof(shared_data_t));
        if (!node->udp_data)
            return;

        node->udp_data->data = NULL;
        if (pthread_mutex_init(&node->udp_data->mutex, NULL) != 0 ||
            pthread_cond_init(&node->udp_data->cond, NULL) != 0)
        {
            free(node->udp_data);
            node->udp_data = NULL;
            return;
        }
    }

    pthread_mutex_lock(&node->udp_data->mutex);

    // Free existing data if present
    if (node->udp_data->data)
    {
        raw_data_t *old_data = (raw_data_t *)node->udp_data->data;
        if (old_data->data)
            free(old_data->data);
        free(old_data);
    }

    // Allocate new raw_data_t structure
    raw_data_t *raw_data = malloc(sizeof(raw_data_t));
    if (raw_data)
    {
        raw_data->length = resp_len;
        raw_data->timestamp = time(NULL);
        raw_data->data = malloc(resp_len);

        if (raw_data->data)
        {
            memcpy(raw_data->data, resp, resp_len);
            node->udp_data->data = raw_data;
            node->last_data_received = raw_data->timestamp;
            pthread_cond_signal(&node->udp_data->cond);

#ifdef DEBUG
            printf("UDP: Updated data for node %d - %d bytes received\n", node->node_id, resp_len);
#endif
        }
        else
        {
            free(raw_data);
        }
    }

    pthread_mutex_unlock(&node->udp_data->mutex);
}

// Main UDP thread function
void *udp_thread_func(void *arg)
{
    udp_config_t *config = get_udp_config();
    if (!config)
        return NULL;

    // 1. INITIALIZATION
    if (udp_init() != 0)
        return NULL;

    // 2. INITIAL CONNECTION
    char server_url[256];
    snprintf(server_url, sizeof(server_url), "udp://%s:%d",
             config->server_host, config->server_port);
    udp_connect(server_url);

    // 3. WAIT FOR CONNECTION WITH TIMEOUT
    int connection_timeout = config->connection_timeout;
    while (!udp_connected && connection_timeout > 0)
    {
#ifdef DEBUG
        printf("UDP: Waiting for connection...\n");
#endif
        mg_mgr_poll(&udp_mgr, 100);
        connection_timeout--;
    }

    if (!udp_connected)
    {
        udp_close();
        return NULL;
    }

    // 4. ALLOCATE BUFFERS
    char *telemetry_payload = malloc(config->payload_buffer_size);
    if (!telemetry_payload)
    {
        udp_close();
        return NULL;
    }

    // 5. MAIN LOOP
    time_t last_publish = 0;
    while (1)
    {
        // Handle pause/resume
        pthread_mutex_lock(&udp_pause.mutex);
        while (udp_pause.is_paused)
        {
            pthread_cond_wait(&udp_pause.cond, &udp_pause.mutex);
        }
        pthread_mutex_unlock(&udp_pause.mutex);

        // Poll events
        mg_mgr_poll(&udp_mgr, 100);

        time_t current_time = time(NULL);

        // Send periodic telemetry
        if (udp_connected && current_time - last_publish >= config->send_interval)
        {
            build_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);
            if (strlen(telemetry_payload) > 0)
            {
                udp_send_data(telemetry_payload, strlen(telemetry_payload));
                last_publish = current_time;
            }
        }

        // Handle reconnection
        if (!udp_connected)
        {
            udp_reconnect();
            usleep(config->reconnect_delay_ms * 1000);
        }
        else
        {
            usleep(config->loop_interval_ms * 1000);
        }
    }

    // 6. CLEANUP
    free(telemetry_payload);
    udp_close();
    return NULL;
}
