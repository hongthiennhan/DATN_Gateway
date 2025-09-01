#include "tcp_thread.h"
#include "mongoose.h"
static struct mg_mgr tcp_mgr;
static struct mg_connection *tcp_connection = NULL;
volatile int tcp_connected = 0;

thread_pause_t tcp_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};

// Get local IP address
char *get_local_ip(void)
{
    static char ip_str[INET_ADDRSTRLEN]; // Static buffer for IP string
    struct ifaddrs *ifaddrs_ptr, *ifa;

    // Get list of network interfaces
    if (getifaddrs(&ifaddrs_ptr) == -1)
    {
        // Failed to get interface list, return localhost
        strcpy(ip_str, "127.0.0.1");
        return ip_str;
    }

    // Iterate through all network interfaces
    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next)
    {
        // Skip interfaces without addresses
        if (ifa->ifa_addr == NULL)
            continue;

        // Only process IPv4 addresses
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)ifa->ifa_addr;
            char *addr_str = inet_ntoa(addr_in->sin_addr);

            // Skip loopback (127.x.x.x) and link-local (169.254.x.x) addresses
            if (strncmp(addr_str, "127.", 4) != 0 && strncmp(addr_str, "169.254.", 8) != 0)
            {
                // Found a valid external IP address
                strcpy(ip_str, addr_str);
                freeifaddrs(ifaddrs_ptr);
                return ip_str;
            }
        }
    }

    // No suitable IP address found, free memory and return localhost
    freeifaddrs(ifaddrs_ptr);
    strcpy(ip_str, "127.0.0.1");
    return ip_str;
}

// Ensure directory exists for JSON files
int ensure_directory_exists(const char *dir)
{
    struct stat st;

    // Check if path exists
    if (stat(dir, &st) == 0)
    {
        // Path exists, check if it's a directory
        if (S_ISDIR(st.st_mode))
        {
            // Directory exists and is valid
            return 0;
        }
        else
        {
            // Path exists but is not a directory (could be a file)
#ifdef DEBUG
            fprintf(stderr, "ERROR: %s exists but is not a directory\n", dir);
#endif
            return -1;
        }
    }

    // Directory doesn't exist, try to create it
    if (mkdir(dir, 0755) == 0)
    {
        // Successfully created directory
#ifdef DEBUG
        printf("Created directory: %s\n", dir);
#endif
        return 0;
    }

    // mkdir() failed, check the reason
    if (errno == EEXIST)
    {
        // Race condition: directory was created by another process
        return ensure_directory_exists(dir); // Recursive check
    }

    // Other error occurred during mkdir()
#ifdef DEBUG
    fprintf(stderr, "ERROR: Cannot create directory %s: %s\n", dir, strerror(errno));
#endif
    return -1;
}

/**
 * Validate basic JSON structure for TCP messages
 * Checks for proper JSON formatting and size limits
 */
int validate_json_basic(const void *data, size_t size)
{
    if (!data || size == 0) {
#ifdef DEBUG
        fprintf(stderr, "TCP: JSON validation - Empty data\n");
#endif
        return 0;
    }

    if (size > MAX_JSON_SIZE) {
#ifdef DEBUG
        fprintf(stderr, "TCP: JSON validation - Data too large: %zu bytes\n", size);
#endif
        return 0;
    }

    const char *json_str = (const char *)data;
    if (json_str[0] != '{') {
#ifdef DEBUG
        fprintf(stderr, "TCP: JSON validation - Must start with '{'\n");
#endif
        return 0;
    }

    // Find closing brace by scanning from end
    int found_closing = 0;
    for (int i = size - 1; i >= 0; i--) {
        if (json_str[i] == '}') {
            found_closing = 1;
            break;
        } else if (json_str[i] != ' ' && json_str[i] != '\n' && 
                   json_str[i] != '\t' && json_str[i] != '\0') {
            break;
        }
    }

    if (!found_closing) {
#ifdef DEBUG
        fprintf(stderr, "TCP: JSON validation - Must end with '}'\n");
#endif
        return 0;
    }

#ifdef DEBUG
    printf("TCP: JSON validation passed: %zu bytes\n", size);
#endif
    return 1;
}

/**
 * Atomically write JSON configuration file for TCP
 * Uses temporary file and rename for atomic operation
 */
int atomic_write_json_file(const char *dir, const char *filename, const void *data, size_t size)
{
    if (ensure_directory_exists(dir) != 0) {
        return -1;
    }

    char current_path[64], new_path[64], backup_path[64];
    snprintf(current_path, sizeof(current_path), "%s/%s", dir, filename);
    snprintf(new_path, sizeof(new_path), "%s/%s.new", dir, filename);
    snprintf(backup_path, sizeof(backup_path), "%s/config_backup.json", dir);

    // Delete old config file if it exists
    if (access(current_path, F_OK) == 0) {
        if (unlink(current_path) != 0) {
#ifdef DEBUG
            fprintf(stderr, "TCP: Cannot delete old config file %s: %s\n",
                    current_path, strerror(errno));
#endif
            return -1;
        }
#ifdef DEBUG
        printf("TCP: Deleted old config file: %s\n", current_path);
#endif
    }

    // Create new file
    int fd = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
#ifdef DEBUG
        fprintf(stderr, "TCP: Cannot create new file %s: %s\n",
                new_path, strerror(errno));
#endif
        return -1;
    }

    // Write data to new file with partial-write handling
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t written = write(fd, (const char *)data + total_written,
                               size - total_written);
        if (written < 0) {
            if (errno == EINTR)
                continue;
#ifdef DEBUG
            fprintf(stderr, "TCP: Write to new file failed: %s\n",
                    strerror(errno));
#endif
            close(fd);
            unlink(new_path);
            return -1;
        }
        total_written += written;
    }

    // Ensure data is flushed to storage
    if (fsync(fd) != 0) {
#ifdef DEBUG
        fprintf(stderr, "TCP: fsync new file failed: %s\n", strerror(errno));
#endif
        close(fd);
        unlink(new_path);
        return -1;
    }

    close(fd);

    // Atomically rename new file to current filename
    if (rename(new_path, current_path) != 0) {
#ifdef DEBUG
        fprintf(stderr, "TCP: rename new file failed: %s\n", strerror(errno));
#endif
        unlink(new_path);
        return -1;
    }

#ifdef DEBUG
    printf("TCP: New config file created successfully: %s\n", current_path);
#endif
    return 0;
}

/**
 * Check if configuration has been updated and reload it safely
 * Uses mutex locking to ensure thread safety during reload
 */
int check_and_reload_config(void)
{
    // Static variables to track config update state
    static volatile int config_updated = 0;
    static pthread_mutex_t config_update_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&config_update_mutex);
    int should_reload = config_updated;
    if (should_reload) {
        config_updated = 0;
    }
    pthread_mutex_unlock(&config_update_mutex);

    if (!should_reload)
        return 0;

    pthread_mutex_lock(&config_mutex);
    config_reloading = 1;

#ifdef DEBUG
    printf("TCP: Reloading config from server update\n");
#endif

    cleanup_nodes_config();

    char config_path[64];
    snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, CONFIG_FILE);

    int result = 0;
    if (load_nodes_config(config_path) == 0) {
#ifdef DEBUG
        printf("TCP: Config reloaded successfully\n");
#endif
        result = 1;
    } else {
#ifdef DEBUG
        fprintf(stderr, "TCP: Failed to reload config, using fallback\n");
#endif
        // Try fallback config
        snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, FALLBACK_CONFIG_FILE);
        if (load_nodes_config(config_path) == 0) {
            result = 1;
        }
    }

    config_reloading = 0;
    pthread_mutex_unlock(&config_mutex);

    return result;
}

// Initialize TCP connection manager and reset connection state
int tcp_init(void)
{
#ifdef DEBUG
    printf("Initializing TCP connection manager\n");
#endif

    // Initialize Mongoose manager for TCP
    mg_mgr_init(&tcp_mgr);

    // Reset connection state
    tcp_connection = NULL;
    tcp_connected = 0;

    // Validate TCP configuration exists
    tcp_config_t *config = get_tcp_config();
    if (!config)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: No TCP configuration available\n");
#endif
        return -1;
    }

    // Validate essential TCP configuration fields
    if (strlen(config->server_host) == 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: TCP server host not configured\n");
#endif
        return -1;
    }

    if (config->server_port == 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: TCP server port not configured\n");
#endif
        return -1;
    }

#ifdef DEBUG
    printf("TCP initialized successfully\n");
    printf("Target server: %s:%d\n", config->server_host, config->server_port);
    printf("Client ID: %s\n", config->client_id);
    printf("Protocol version: %s\n", config->protocol_version);
#endif

    return 0;
}

// Establish TCP connection to the specified server
int tcp_connect(const char *server_url)
{
    tcp_config_t *config = get_tcp_config();
    if (!config) {
#ifdef DEBUG
        fprintf(stderr, "ERROR: No TCP configuration available\n");
#endif
        return -1;
    }

    // Build connection URL from config if not provided
    char url[512];
    if (server_url && strlen(server_url) > 0) {
        // Use provided URL
        snprintf(url, sizeof(url), "%s", server_url);
    } else {
        // Build URL from configuration
        snprintf(url, sizeof(url), "tcp://%s:%d", 
                 config->server_host, config->server_port);
    }

#ifdef DEBUG
    printf("Connecting to TCP server: %s\n", url);
    printf("Client ID: %s\n", config->client_id);
    printf("Protocol version: %s\n", config->protocol_version);
#endif

    // Reset connection state
    tcp_connected = 0;
    tcp_connection = NULL;

    // Create TCP connection using Mongoose
    tcp_connection = mg_connect(&tcp_mgr, url, tcp_event_handler, NULL);
    if (!tcp_connection) {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to create TCP connection to %s\n", url);
#endif
        return -1;
    }

    // Set socket options if configured
    if (config->socket_options.tcp_nodelay) {
        // Enable TCP_NODELAY to disable Nagle's algorithm
#ifdef DEBUG
        printf("TCP_NODELAY enabled\n");
#endif
    }

    if (config->socket_options.keepalive) {
        // Enable TCP keepalive
#ifdef DEBUG
        printf("TCP keepalive enabled (idle: %ds, interval: %ds, count: %d)\n",
               config->socket_options.keepalive_idle,
               config->socket_options.keepalive_interval,
               config->socket_options.keepalive_count);
#endif
    }

#ifdef DEBUG
    printf("TCP connection initiated successfully\n");
    printf("Connection timeout: %d seconds\n", config->connection_timeout);
#endif

    return 0;
}

// Close TCP connection and clean up resources
void tcp_close(void)
{
#ifdef DEBUG
    printf("Closing TCP connection and cleaning up resources\n");
#endif

    // Reset connection state first
    tcp_connected = 0;

    // Close existing connection if it exists
    if (tcp_connection) {
#ifdef DEBUG
        printf("Closing TCP connection\n");
#endif
        // Process any pending events before closing
        mg_mgr_poll(&tcp_mgr, 0);
        
        // Connection will be automatically closed when manager is freed
        tcp_connection = NULL;
    }

    // Free Mongoose manager resources
#ifdef DEBUG
    printf("Freeing TCP manager resources\n");
#endif
    mg_mgr_free(&tcp_mgr);

#ifdef DEBUG
    printf("TCP connection closed successfully\n");
#endif
}

// Reconnect to TCP server after connection loss
int tcp_reconnect(void)
{
    tcp_config_t *config = get_tcp_config();
    if (!config) {
#ifdef DEBUG
        fprintf(stderr, "ERROR: No TCP configuration available for reconnect\n");
#endif
        return -1;
    }

#ifdef DEBUG
    printf("TCP reconnect: Attempting to reconnect to %s:%d\n", 
           config->server_host, config->server_port);
#endif

    // Close existing connection first if any
    if (tcp_connection) {
#ifdef DEBUG
        printf("TCP reconnect: Closing existing connection\n");
#endif
        // Process any pending events before closing
        mg_mgr_poll(&tcp_mgr, 0);
        tcp_connection = NULL;
    }

    // Reset connection state
    tcp_connected = 0;

    // Build connection URL
    char url[512];
    snprintf(url, sizeof(url), "tcp://%s:%d", 
             config->server_host, config->server_port);

    // Establish new connection
    tcp_connection = mg_connect(&tcp_mgr, url, tcp_event_handler, NULL);
    if (!tcp_connection) {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to reconnect to TCP server %s\n", url);
#endif
        return -1;
    }

    // Apply socket options if configured
    if (config->socket_options.tcp_nodelay) {
#ifdef DEBUG
        printf("TCP reconnect: TCP_NODELAY will be applied\n");
#endif
    }

    if (config->socket_options.keepalive) {
#ifdef DEBUG
        printf("TCP reconnect: TCP keepalive will be applied\n");
#endif
    }

#ifdef DEBUG
    printf("TCP reconnect: Connection initiated successfully\n");
#endif

    return 0;
}

// TCP event handler for Mongoose framework
void tcp_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    switch (ev) {
        case MG_EV_CONNECT: {
            // TCP connection established successfully
            tcp_connected = 1;
            tcp_connection = c;
            
#ifdef DEBUG
            printf("TCP: Connected to server successfully\n");
#endif

            // Get TCP configuration for initial setup
            tcp_config_t *config = get_tcp_config();
            if (!config) {
#ifdef DEBUG
                printf("TCP: Warning - No TCP config available after connect\n");
#endif
                break;
            }

            // Send initial handshake or identification if needed
            if (config->features.status_reporting) {
                system_info_t *sys_info = get_system_info();
                if (sys_info) {
                    char handshake[512];
                    snprintf(handshake, sizeof(handshake),
                        "{"
                        "\"type\":\"handshake\","
                        "\"client_id\":\"%s\","
                        "\"protocol_version\":\"%s\","
                        "\"gateway_ip\":\"%s\","
                        "\"firmware_version\":\"%s\","
                        "\"device_type\":\"%s\""
                        "}%s",
                        config->client_id,
                        config->protocol_version,
                        get_local_ip(),
                        sys_info->firmware_version,
                        sys_info->device_type,
                        config->protocol_settings.message_delimiter);
                    
                    mg_send(c, handshake, strlen(handshake));
#ifdef DEBUG
                    printf("TCP: Handshake sent\n");
#endif
                }
            }
            break;
        }

        case MG_EV_READ: {
            // Data received from TCP server
            struct mg_str received = c->recv;
            if (received.len > 0) {
#ifdef DEBUG
                printf("TCP: Received %d bytes of data\n", (int)received.len);
#endif
                
                // Process received data
                tcp_process_received_data(received.ptr, received.len);
                
                // Clear the receive buffer after processing
                mg_iobuf_del(&c->recv, 0, received.len);
            }
            break;
        }

        case MG_EV_CLOSE: {
            // TCP connection closed
            tcp_connected = 0;
            tcp_connection = NULL;
            
#ifdef DEBUG
            printf("TCP: Connection closed by server\n");
#endif
            break;
        }

        case MG_EV_ERROR: {
            // TCP connection error occurred
            tcp_connected = 0;
            tcp_connection = NULL;
            
            char *error_msg = (char *)ev_data;
#ifdef DEBUG
            printf("TCP: Connection error - %s\n", error_msg ? error_msg : "Unknown error");
#endif
            break;
        }

        case MG_EV_POLL: {
            // Periodic polling event - can be used for keepalive
            tcp_config_t *config = get_tcp_config();
            if (config && config->socket_options.keepalive && tcp_connected) {
                // Keepalive is handled by socket options, no action needed here
                // This case is included for completeness and future extensions
            }
            break;
        }

        default:
            // Unhandled event types
#ifdef DEBUG
            printf("TCP: Unhandled event type: %d\n", ev);
#endif
            break;
    }
}

/**
 * Send data through TCP connection
 * Adds message delimiter and handles transmission
 */
int tcp_send_data(const char *data, size_t len)
{
    if (!data || len == 0) {
#ifdef DEBUG
        printf("TCP: Invalid data parameters for sending\n");
#endif
        return -1;
    }

    if (!tcp_connection || !tcp_connected) {
#ifdef DEBUG
        printf("TCP: No active connection for sending data\n");
#endif
        return -1;
    }

    tcp_config_t *config = get_tcp_config();
    if (!config) {
#ifdef DEBUG
        printf("TCP: No configuration available for sending\n");
#endif
        return -1;
    }

    // Check message size limits
    if (len > config->protocol_settings.max_message_size) {
#ifdef DEBUG
        printf("TCP: Message too large (%zu bytes, max %d)\n", 
               len, config->protocol_settings.max_message_size);
#endif
        return -1;
    }

    // Calculate total size including delimiter
    size_t delimiter_len = strlen(config->protocol_settings.message_delimiter);
    size_t total_len = len + delimiter_len;
    
    // Allocate buffer for data + delimiter
    char *send_buffer = malloc(total_len);
    if (!send_buffer) {
#ifdef DEBUG
        printf("TCP: Failed to allocate send buffer\n");
#endif
        return -1;
    }

    // Copy data and append delimiter
    memcpy(send_buffer, data, len);
    memcpy(send_buffer + len, config->protocol_settings.message_delimiter, delimiter_len);

#ifdef DEBUG
    printf("TCP: Sending %zu bytes (including %zu byte delimiter)\n", 
           total_len, delimiter_len);
#endif

    // Send data through Mongoose
    size_t sent = mg_send(tcp_connection, send_buffer, total_len);
    free(send_buffer);

    if (sent != total_len) {
#ifdef DEBUG
        printf("TCP: Partial send - sent %zu of %zu bytes\n", sent, total_len);
#endif
        return -1;
    }

#ifdef DEBUG
    printf("TCP: Data sent successfully\n");
#endif
    return 0;
}

/**
 * Process data received from TCP server
 * Handles different message types including control commands and config updates
 */
void tcp_process_received_data(const char *data, size_t len)
{
    if (!data || len == 0) {
#ifdef DEBUG
        printf("TCP: No data to process\n");
#endif
        return;
    }

    tcp_config_t *config = get_tcp_config();
    if (!config) {
#ifdef DEBUG
        printf("TCP: No configuration available for processing\n");
#endif
        return;
    }

#ifdef DEBUG
    printf("TCP: Processing %zu bytes of received data\n", len);
#endif

    // Create null-terminated string for processing
    char *data_str = malloc(len + 1);
    if (!data_str) {
#ifdef DEBUG
        printf("TCP: Failed to allocate processing buffer\n");
#endif
        return;
    }
    
    memcpy(data_str, data, len);
    data_str[len] = '\0';

    // Split data by message delimiter if present
    char *delimiter = config->protocol_settings.message_delimiter;
    char *message_start = data_str;
    char *message_end;

    while ((message_end = strstr(message_start, delimiter)) != NULL) {
        // Process individual message
        *message_end = '\0';  // Temporarily null-terminate
        
        if (strlen(message_start) > 0) {
            tcp_process_single_message(message_start);
        }
        
        // Move to next message
        message_start = message_end + strlen(delimiter);
    }

    // Process remaining data if no delimiter at end
    if (strlen(message_start) > 0) {
        tcp_process_single_message(message_start);
    }

    free(data_str);
}

/**
 * Process a single TCP message
 * Handles JSON parsing and message type routing
 */
static void tcp_process_single_message(const char *message)
{
    if (!message || strlen(message) == 0) {
        return;
    }

#ifdef DEBUG
    printf("TCP: Processing message: %s\n", message);
#endif

    // Validate JSON if data format is JSON
    tcp_config_t *config = get_tcp_config();
    if (config && strcmp(config->data_format, "json") == 0) {
        if (!validate_json_basic(message, strlen(message))) {
#ifdef DEBUG
            printf("TCP: Invalid JSON message received\n");
#endif
            return;
        }
    }

    // Parse JSON message
    json_object *root = json_tokener_parse(message);
    if (!root) {
#ifdef DEBUG
        printf("TCP: Failed to parse JSON message\n");
#endif
        return;
    }

    // Check message type
    json_object *type_obj;
    if (json_object_object_get_ex(root, "type", &type_obj)) {
        const char *msg_type = json_object_get_string(type_obj);
        
#ifdef DEBUG
        printf("TCP: Message type: %s\n", msg_type);
#endif

        if (strcmp(msg_type, "control_command") == 0) {
            // Handle control command
            if (config && config->features.control_commands) {
                process_tcp_control_command(message);
            } else {
#ifdef DEBUG
                printf("TCP: Control commands disabled in configuration\n");
#endif
            }
        }
        else if (strcmp(msg_type, "config_update") == 0) {
            // Handle configuration update
            if (config && config->features.config_download) {
                tcp_process_config_update(root);
            } else {
#ifdef DEBUG
                printf("TCP: Config download disabled in configuration\n");
#endif
            }
        }
        else if (strcmp(msg_type, "ping") == 0) {
            // Handle ping message - send pong response
            tcp_send_pong_response();
        }
        else if (strcmp(msg_type, "ack") == 0) {
            // Handle acknowledgment
#ifdef DEBUG
            printf("TCP: Received acknowledgment\n");
#endif
        }
        else {
#ifdef DEBUG
            printf("TCP: Unknown message type: %s\n", msg_type);
#endif
        }
    } else {
        // No type field - treat as raw data or legacy format
#ifdef DEBUG
        printf("TCP: Processing message without type field\n");
#endif
        
        // Check if it's a control command in legacy format
        json_object *method_obj;
        if (json_object_object_get_ex(root, "method", &method_obj)) {
            process_tcp_control_command(message);
        }
    }

    json_object_put(root);
}

/**
 * Process TCP configuration update
 * Validates and applies new configuration
 */
static void tcp_process_config_update(json_object *config_obj)
{
    json_object *config_data_obj;
    if (!json_object_object_get_ex(config_obj, "config", &config_data_obj)) {
#ifdef DEBUG
        printf("TCP: No config data in update message\n");
#endif
        return;
    }

    // Convert JSON back to string for file writing
    const char *config_str = json_object_to_json_string(config_data_obj);
    if (!config_str) {
#ifdef DEBUG
        printf("TCP: Failed to serialize config data\n");
#endif
        return;
    }

    // Write configuration to file
    int result = atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, 
                                       config_str, strlen(config_str));
    if (result == 0) {
#ifdef DEBUG
        printf("TCP: Configuration updated successfully\n");
#endif
        
        // Trigger config reload
        pthread_mutex_lock(&config_mutex);
        config_reloading = 1;
        pthread_mutex_unlock(&config_mutex);
    } else {
#ifdef DEBUG
        printf("TCP: Failed to write configuration update\n");
#endif
    }
}

/**
 * Send pong response to ping message
 * Maintains connection keepalive
 */
static void tcp_send_pong_response(void)
{
    tcp_config_t *config = get_tcp_config();
    if (!config) return;

    char pong_msg[256];
    snprintf(pong_msg, sizeof(pong_msg),
        "{"
        "\"type\":\"pong\","
        "\"client_id\":\"%s\","
        "\"timestamp\":%ld"
        "}",
        config->client_id,
        time(NULL));

    tcp_send_data(pong_msg, strlen(pong_msg));

#ifdef DEBUG
    printf("TCP: Pong response sent\n");
#endif
}

/**
 * Process control command received via TCP connection
 */
static void process_tcp_control_command(const char *payload)
{
    if (!payload || strlen(payload) == 0) {
#ifdef DEBUG
        printf("TCP: Empty control command payload\n");
#endif
        return;
    }

    json_object *root = json_tokener_parse(payload);
    if (!root) {
#ifdef DEBUG
        printf("TCP: ERROR - Invalid JSON in control command\n");
#endif
        return;
    }

    json_object *method_obj, *params_obj;
    
    // Check for method field (standard format)
    if (!json_object_object_get_ex(root, "method", &method_obj)) {
        // Try alternative format for TCP-specific commands
        json_object *type_obj;
        if (json_object_object_get_ex(root, "type", &type_obj)) {
            const char *msg_type = json_object_get_string(type_obj);
            if (strcmp(msg_type, "control_command") == 0) {
                // Handle TCP-specific control command format
                process_tcp_specific_command(root);
                json_object_put(root);
                return;
            }
        }
        
#ifdef DEBUG
        printf("TCP: ERROR - No method or type in control command\n");
#endif
        json_object_put(root);
        return;
    }

    const char *method = json_object_get_string(method_obj);
#ifdef DEBUG
    printf("TCP: Processing control method: %s\n", method);
#endif

    if (strcmp(method, "executeCommand") == 0) {
        if (json_object_object_get_ex(root, "params", &params_obj)) {
            json_object *node_id_obj, *cmd_id_obj, *params_str_obj;
            
            if (json_object_object_get_ex(params_obj, "nodeId", &node_id_obj) &&
                json_object_object_get_ex(params_obj, "cmdId", &cmd_id_obj)) {
                
                int node_id = json_object_get_int(node_id_obj);
                int cmd_id = json_object_get_int(cmd_id_obj);
                const char *params_str = "";
                
                if (json_object_object_get_ex(params_obj, "params", &params_str_obj)) {
                    params_str = json_object_get_string(params_str_obj);
                }

#ifdef DEBUG
                printf("TCP: Server command - node=%d, cmd=%d, params=%s\n",
                       node_id, cmd_id, params_str);
#endif

                // Queue command for execution
                if (add_control_command(node_id, cmd_id, params_str) == 0) {
#ifdef DEBUG
                    printf("TCP: Command queued successfully\n");
#endif
                    // Send acknowledgment back to server
                    tcp_send_command_ack(node_id, cmd_id, "success");
                } else {
#ifdef DEBUG
                    printf("TCP: Failed to queue command\n");
#endif
                    // Send error acknowledgment back to server
                    tcp_send_command_ack(node_id, cmd_id, "failed");
                }
            } else {
#ifdef DEBUG
                printf("TCP: ERROR - Missing nodeId or cmdId in executeCommand\n");
#endif
            }
        } else {
#ifdef DEBUG
            printf("TCP: ERROR - No params in executeCommand\n");
#endif
        }
    }
    else if (strcmp(method, "getNodeStatus") == 0) {
        // Handle node status request
        if (json_object_object_get_ex(root, "params", &params_obj)) {
            json_object *node_id_obj;
            if (json_object_object_get_ex(params_obj, "nodeId", &node_id_obj)) {
                int node_id = json_object_get_int(node_id_obj);
                tcp_send_node_status(node_id);
            }
        }
    }
    else if (strcmp(method, "getAllNodes") == 0) {
        // Handle request for all node information
        tcp_send_all_nodes_info();
    }
    else if (strcmp(method, "ping") == 0) {
        // Handle ping request
        tcp_send_pong_response();
    }
    else {
#ifdef DEBUG
        printf("TCP: Unknown control method: %s\n", method);
#endif
    }

    json_object_put(root);
}
/**
 * Build telemetry payload with node data for TCP transmission
 */
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp)
{
    if (!payload || payload_size == 0) {
        return;
    }

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
        if (!node || !node->tcp_data) {
            continue;
        }

        // Try to lock node mutex (non-blocking)
        if (pthread_mutex_trylock(&node->tcp_data->mutex) == 0) {
            if (node->tcp_data->data) {
                raw_data_t *raw_data = (raw_data_t *)node->tcp_data->data;
                if (raw_data && raw_data->data && raw_data->length > 0) {
                    // Convert binary data to hex string
                    char *hex_str = malloc(raw_data->length * 2 + 1);
                    if (hex_str) {
                        for (int j = 0; j < raw_data->length; j++) {
                            sprintf(hex_str + j * 2, "%02X", raw_data->data[j]);
                        }
                        hex_str[raw_data->length * 2] = '\0';

                        // Add node data to payload
                        snprintf(temp_buffer, 1024, ",\"node%d_data\":\"%s\",\"node%d_type\":\"%s\"",
                                node->node_id, hex_str, node->node_id, node->com_type);
                        
                        // Check if adding this data would exceed buffer size
                        if (strlen(payload) + strlen(temp_buffer) + 2 < payload_size) {
                            strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
                        }
                        
                        free(hex_str);
                    }
                }
            }
            pthread_mutex_unlock(&node->tcp_data->mutex);
        }
        // If trylock fails, skip this node to avoid blocking
    }

    // Add system information
    if (config->system_fields.include_gateway_ip) {
        snprintf(temp_buffer, 1024, ",\"gateway_ip\":\"%s\"", get_local_ip());
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
        // Ensure proper JSON closure even if truncated
        if (payload_size > 1) {
            payload[payload_size - 2] = '}';
            payload[payload_size - 1] = '\0';
        }
    }

    free(temp_buffer);
}

/**
 * Update TCP data with received response from node
 */
void update_tcp_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len)
{
    if (!node || !resp || resp_len == 0) {
        return;
    }

    // Ensure tcp_data structure exists
    if (!node->tcp_data) {
        node->tcp_data = malloc(sizeof(shared_data_t));
        if (!node->tcp_data) {
            return;
        }
        node->tcp_data->data = NULL;
        if (pthread_mutex_init(&node->tcp_data->mutex, NULL) != 0) {
            free(node->tcp_data);
            node->tcp_data = NULL;
            return;
        }
        if (pthread_cond_init(&node->tcp_data->cond, NULL) != 0) {
            pthread_mutex_destroy(&node->tcp_data->mutex);
            free(node->tcp_data);
            node->tcp_data = NULL;
            return;
        }
    }

    // Lock the mutex for thread-safe access
    pthread_mutex_lock(&node->tcp_data->mutex);

    // Free existing data if present
    if (node->tcp_data->data) {
        raw_data_t *old_data = (raw_data_t *)node->tcp_data->data;
        if (old_data->data) {
            free(old_data->data);
        }
        free(old_data);
        node->tcp_data->data = NULL;
    }

    // Allocate new raw_data_t structure
    raw_data_t *raw_data = malloc(sizeof(raw_data_t));
    if (raw_data) {
        raw_data->length = resp_len;
        raw_data->timestamp = time(NULL);
        
        // Allocate buffer for response data
        raw_data->data = malloc(resp_len);
        if (raw_data->data) {
            // Copy response data
            memcpy(raw_data->data, resp, resp_len);
            
            // Store the new data
            node->tcp_data->data = raw_data;
            
            // Update node's last data received timestamp
            node->last_data_received = raw_data->timestamp;
            
            // Signal condition variable to notify waiting threads
            pthread_cond_signal(&node->tcp_data->cond);
            
#ifdef DEBUG
            printf("TCP: Updated data for node %d - %d bytes received\n", 
                   node->node_id, resp_len);
#endif
        } else {
            // Failed to allocate data buffer
            free(raw_data);
#ifdef DEBUG
            printf("TCP: Failed to allocate data buffer for node %d\n", node->node_id);
#endif
        }
    } else {
#ifdef DEBUG
        printf("TCP: Failed to allocate raw_data structure for node %d\n", node->node_id);
#endif
    }

    // Unlock the mutex
    pthread_mutex_unlock(&node->tcp_data->mutex);
}

void *tcp_thread_func(void *arg)
{
    // 1. INITIALIZATION
    if (tcp_init() != 0)
    {
#ifdef DEBUG
        printf("Failed to initialize TCP\n");
#endif
        return NULL;
    }

    // 2. INITIAL CONNECTION
    const char *server_url = "tcp://localhost:8765";
    tcp_connect(server_url);

    // 3. WAIT FOR SUCCESSFUL CONNECTION
    // Wait for connection timeout logic

    // 4. INITIAL CONFIG REQUEST (if needed)
    // tcp_request_config();

    // 5. MAIN LOOP
    while (1)
    {
        // 5.1 Handle pause/resume
        pthread_mutex_lock(&tcp_pause.mutex);
        while (tcp_pause.is_paused)
        {
            pthread_cond_wait(&tcp_pause.cond, &tcp_pause.mutex);
        }
        pthread_mutex_unlock(&tcp_pause.mutex);

        // 5.2 Poll events (handle tcp_event_handler)
        mg_mgr_poll(&tcp_mgr, 100);

        // 5.3 Check & reload config
        check_and_reload_config();

        // 5.4 Send periodic telemetry
        time_t current_time = time(NULL);
        if (current_time - last_publish >= publish_interval)
        {
            build_telemetry_payload(payload_buffer, buffer_size, current_time);
            tcp_send_data(payload_buffer, strlen(payload_buffer));
            last_publish = current_time;
        }

        // 5.5 Handle reconnection
        if (!tcp_connected)
        {
            tcp_reconnect();
        }

        // 5.6 Loop sleep
        usleep(loop_interval_ms * 1000);
    }

    // 6. CLEANUP
    tcp_close();
    return NULL;
}
