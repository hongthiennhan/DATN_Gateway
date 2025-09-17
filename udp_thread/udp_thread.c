#include "udp_thread.h"

// Static variables for UDP management
static struct mg_mgr udp_mgr;
static struct mg_connection *udp_connection = NULL;
volatile int udp_connected = 0;

thread_pause_t udp_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};

// Static buffer for storing received data
static pthread_mutex_t udp_data_mutex = PTHREAD_MUTEX_INITIALIZER;
static char udp_last_received_data[MAX_RECEIVED_DATA_SIZE] = {0};
static time_t udp_last_received_time = 0;
static int udp_total_messages_received = 0;

// Forward declarations
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

// Helper function to escape JSON strings
static void escape_json_string(const char *src, char *dst, size_t dst_size)
{
    if (!src || !dst || dst_size == 0)
        return;

    size_t src_len = strlen(src);
    size_t dst_idx = 0;

    for (size_t i = 0; i < src_len && dst_idx < dst_size - 1; i++)
    {
        if (src[i] == '"' || src[i] == '\\')
        {
            if (dst_idx < dst_size - 2)
            {
                dst[dst_idx++] = '\\';
                dst[dst_idx++] = src[i];
            }
        }
        else if (src[i] >= 32 && src[i] <= 126)
        { // Printable ASCII
            dst[dst_idx++] = src[i];
        }
    }
    dst[dst_idx] = '\0';
}

// Helper function to convert data to hex string
static char *data_to_hex_string(const unsigned char *data, size_t len)
{
    if (!data || len == 0)
        return NULL;

    char *hex_str = malloc(len * 2 + 1);
    if (!hex_str)
        return NULL;

    for (size_t i = 0; i < len; i++)
    {
        sprintf(hex_str + i * 2, "%02X", data[i]);
    }
    hex_str[len * 2] = '\0';

    return hex_str;
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
static void udp_send_pong_response(void)
{
    udp_config_t *config = get_udp_config();
    if (!config)
        return;

    char pong_msg[256];
    snprintf(pong_msg, sizeof(pong_msg),
             "{\"type\":\"pong\",\"client_id\":\"%s\",\"timestamp\":%ld}",
             config->client_id, time(NULL));
    udp_send_data(pong_msg, strlen(pong_msg));

#ifdef DEBUG
    printf("UDP: Sent pong response\n");
#endif
}

// Update received data from external tasks - PUBLIC FUNCTION
void update_udp_received_data(const unsigned char *data, uint16_t data_len)
{
    if (!data || data_len == 0)
        return;

    pthread_mutex_lock(&udp_data_mutex);
    // Store raw data (limit to buffer size)
    size_t copy_len = data_len < sizeof(udp_last_received_data) - 1 ? data_len : sizeof(udp_last_received_data) - 1;
    memcpy(udp_last_received_data, data, copy_len);
    udp_last_received_data[copy_len] = '\0';

    // Update metadata
    udp_last_received_time = time(NULL);
    udp_total_messages_received++;

    pthread_mutex_unlock(&udp_data_mutex);

#ifdef DEBUG
    printf("UDP: Updated received data - %d bytes\n", data_len);
#endif
}

// Clear stored received data - PUBLIC FUNCTION
void clear_udp_received_data(void)
{
    pthread_mutex_lock(&udp_data_mutex);
    memset(udp_last_received_data, 0, sizeof(udp_last_received_data));
    udp_last_received_time = 0;
    udp_total_messages_received = 0;
    pthread_mutex_unlock(&udp_data_mutex);
}

// Process received UDP data - simplified without JSON parsing
static void udp_process_received_data(const char *data, size_t len)
{
    if (!data || len == 0)
        return;

    // Store received data for telemetry
    update_udp_received_data((const unsigned char *)data, len);

    // Create null-terminated string for ping detection
    char *msg = malloc(len + 1);
    if (!msg)
        return;

    memcpy(msg, data, len);
    msg[len] = '\0';

    // Check for ping message
    if (strcmp(msg, "ping") == 0 || strstr(msg, "\"type\":\"ping\""))
    {
        udp_send_pong_response();
    }

    free(msg);

#ifdef DEBUG
    printf("UDP: Processed %zu bytes\n", len);
#endif
}

// UDP event handler - simplified without JSON processing
static void udp_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    switch (ev)
    {
    case MG_EV_CONNECT:
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

    case MG_EV_READ:
        if (c->recv.len > 0)
        {
            udp_process_received_data((const char *)c->recv.buf, c->recv.len);
            mg_iobuf_del(&c->recv, 0, c->recv.len);
        }
        break;

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

// Build telemetry payload with gateway system data and received data
static void build_udp_telemetry_payload(char *payload, size_t payload_size, time_t timestamp)
{
    if (!payload || payload_size == 0)
        return;

    udp_config_t *config = get_udp_config();
    if (!config)
    {
        payload[0] = '\0';
        return;
    }

    char *temp_buffer = malloc(2048);
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

    // Add gateway system information
    system_info_t *sys_info = get_system_info();
    if (sys_info)
    {
        snprintf(temp_buffer, 2048, ",\"firmware_version\":\"%s\"", sys_info->firmware_version);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

        snprintf(temp_buffer, 2048, ",\"device_type\":\"%s\"", sys_info->device_type);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

        snprintf(temp_buffer, 2048, ",\"manufacturer\":\"%s\"", sys_info->manufacturer);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

        snprintf(temp_buffer, 2048, ",\"model\":\"%s\"", sys_info->model);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }

    // Add system information
    if (config->system_fields.include_gateway_ip)
    {
        snprintf(temp_buffer, 2048, ",\"gateway_ip\":\"%s\"", udp_get_local_ip());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }

    if (config->system_fields.include_system_info && sys_info)
    {
        snprintf(temp_buffer, 2048, ",\"status\":\"online\"");
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }

    // Add received data information
    pthread_mutex_lock(&udp_data_mutex);
    if (strlen(udp_last_received_data) > 0)
    {
        // Add escaped text data
        char escaped_data[1024];
        escape_json_string(udp_last_received_data, escaped_data, sizeof(escaped_data));
        snprintf(temp_buffer, 2048, ",\"last_received_data\":\"%s\"", escaped_data);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

        // Add hex representation
        char *hex_data = data_to_hex_string((const unsigned char *)udp_last_received_data,
                                            strlen(udp_last_received_data));
        if (hex_data)
        {
            snprintf(temp_buffer, 2048, ",\"last_received_data_hex\":\"%s\"", hex_data);
            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
            free(hex_data);
        }

        // Add metadata
        snprintf(temp_buffer, 2048, ",\"last_received_time\":%ld", udp_last_received_time);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

        snprintf(temp_buffer, 2048, ",\"total_messages_received\":%d", udp_total_messages_received);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    pthread_mutex_unlock(&udp_data_mutex);

    // Close JSON object
    strncat(payload, "}", payload_size - strlen(payload) - 1);
    free(temp_buffer);
}

// Main UDP thread function - simplified version
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

        // Get current config
        config = get_udp_config();
        if (!config)
        {
#ifdef DEBUG
            printf("Config no longer available, exiting UDP thread\n");
#endif
            break;
        }

        // Send periodic telemetry
        if (udp_connected && current_time - last_publish >= config->send_interval)
        {
            build_udp_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);
            if (strlen(telemetry_payload) > 0)
            {
                udp_send_data(telemetry_payload, strlen(telemetry_payload));
                last_publish = current_time;
#ifdef DEBUG
                printf("UDP telemetry sent: %s\n", telemetry_payload);
#endif
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
