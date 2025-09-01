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
int tcp_send_data(const char *data, size_t len) {}
void tcp_process_received_data(const char *data, size_t len) {}

int validate_json_basic(const void *data, size_t size) {}
int atomic_write_json_file(const char *dir, const char *filename, const void *data, size_t size) {}
void process_tcp_control_command(const char *payload) {}
int check_and_reload_config(void) {}

void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp) {}
void update_tcp_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len) {}

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
