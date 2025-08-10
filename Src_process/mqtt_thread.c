#include "thread_func.h"
#include "node_config.h"

// ===== JSON CONFIG DOWNLOAD CONFIGURATION =====
#define CONFIG_DIR  "/home/trieunguyen/Linux_worldspace/Config_Node_Files"
#define CONFIG_FILE "config.json"
#define MAX_JSON_SIZE (1024 * 1024)  // 1MB limit

/**
 * Get local IP address, excluding loopback and link-local addresses
 * This function scans all network interfaces to find a valid public IP
 * @return: Static string containing the local IP address
 */
char* get_local_ip() {
    static char ip_str[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddrs_ptr, *ifa;
    
    // Get list of all network interfaces
    if (getifaddrs(&ifaddrs_ptr) == -1) {
        strcpy(ip_str, "127.0.0.1");
        return ip_str;
    }
    
    // Iterate through all interfaces to find a valid public IP
    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        // Check if this is an IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            char *addr_str = inet_ntoa(addr_in->sin_addr);
            
            // Skip loopback (127.x.x.x) and link-local (169.254.x.x) addresses
            if (strncmp(addr_str, "127.", 4) != 0 && 
                strncmp(addr_str, "169.254.", 8) != 0) {
                strcpy(ip_str, addr_str);
                freeifaddrs(ifaddrs_ptr);
                return ip_str;
            }
        }
    }
    
    // If no valid public IP found, return localhost
    freeifaddrs(ifaddrs_ptr);
    strcpy(ip_str, "127.0.0.1");
    return ip_str;
}

// ===== GLOBAL MQTT VARIABLES =====
struct mosquitto *mqtt_client = NULL;    // Main MQTT client instance
volatile int mqtt_connected = 0;         // Connection status flag (thread-safe)

// ===== ROBUST DIRECTORY CREATION WITH ERROR HANDLING =====
/**
 * Ensure directory exists with comprehensive error handling and race condition protection
 * This function handles multiple threads trying to create the same directory simultaneously
 * @param dir: Directory path to create/verify
 * @return: 0 on success, -1 on error
 */
static int ensure_directory_exists(const char *dir) {
    struct stat st;
    
    // Step 1: Check if path already exists
    if (stat(dir, &st) == 0) {
        // Path exists, verify it's a directory
        if (S_ISDIR(st.st_mode)) {
            return 0;  // SUCCESS: It's a directory
        } else {
            fprintf(stderr, "ERROR: %s exists but is not a directory\n", dir);
            return -1;  // ERROR: It's a file, not directory
        }
    }
    
    // Step 2: Path doesn't exist, try to create it
    if (mkdir(dir, 0755) == 0) {
        printf("Created directory: %s\n", dir);
        return 0;  // SUCCESS: Directory created
    }
    
    // Step 3: Handle race condition - another thread might have created it
    if (errno == EEXIST) {
        // Another thread created the directory, verify it recursively
        return ensure_directory_exists(dir);
    }
    
    // Step 4: Real error occurred
    fprintf(stderr, "ERROR: Cannot create directory %s: %s\n", dir, strerror(errno));
    return -1;
}

// ===== JSON VALIDATION WITH SIZE AND FORMAT CHECKING =====
/**
 * Validate JSON data for basic correctness and size limits
 * Performs lightweight validation to prevent malformed data from corrupting the system
 * @param data: Raw JSON data buffer
 * @param size: Size of JSON data in bytes
 * @return: 1 if valid, 0 if invalid
 */
static int validate_json_basic(const void *data, size_t size) {
    // Check for null or empty data
    if (!data || size == 0) {
        fprintf(stderr, "ERROR: Empty JSON data\n");
        return 0;
    }
    
    // Check size limit to prevent memory exhaustion
    if (size > MAX_JSON_SIZE) {
        fprintf(stderr, "ERROR: JSON too large: %zu bytes (max %d)\n", size, MAX_JSON_SIZE);
        return 0;
    }
    
    const char *json_str = (const char*)data;
    
    // Basic structural validation - must start with '{'
    if (json_str[0] != '{') {
        fprintf(stderr, "ERROR: JSON must start with '{'\n");
        return 0;
    }
    
    // Find closing '}' while ignoring trailing whitespace
    int found_closing = 0;
    for (int i = size - 1; i >= 0; i--) {
        if (json_str[i] == '}') {
            found_closing = 1;
            break;
        } else if (json_str[i] != ' ' && json_str[i] != '\n' && 
                   json_str[i] != '\t' && json_str[i] != '\0') {
            break;  // Found non-whitespace character before '}'
        }
    }
    
    if (!found_closing) {
        fprintf(stderr, "ERROR: JSON must end with '}'\n");
        return 0;
    }
    
    printf("JSON validation passed: %zu bytes\n", size);
    return 1;  // VALID
}

// ===== ATOMIC FILE WRITE WITH FULL DURABILITY GUARANTEES =====
/**
 * Write JSON file atomically with comprehensive error handling
 * Uses write-fsync-rename-fsync pattern to ensure data durability even during power loss
 * This is the core function that guarantees no partial/corrupted files will ever exist
 * @param dir: Target directory path
 * @param filename: Target filename (without path)
 * @param data: JSON data to write
 * @param size: Size of JSON data
 * @return: 0 on success, -1 on error (with cleanup)
 */
static int atomic_write_json_file(const char *dir, const char *filename, const void *data, size_t size) {
    // Step 1: Ensure target directory exists
    if (ensure_directory_exists(dir) != 0) {
        return -1;
    }
    
    // Step 2: Build temporary and final file paths
    char tmp_path[512], final_path[512];
    int ret = snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", dir, filename);
    if (ret >= sizeof(tmp_path)) {
        fprintf(stderr, "ERROR: Path too long for tmp_path\n");
        return -1;
    }
    
    ret = snprintf(final_path, sizeof(final_path), "%s/%s", dir, filename);
    if (ret >= sizeof(final_path)) {
        fprintf(stderr, "ERROR: Path too long for final_path\n");
        return -1;
    }
    
    printf("Writing JSON to: %s (via %s)\n", final_path, tmp_path);
    
    // Step 3: Open temporary file with proper flags
    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Cannot open %s: %s\n", tmp_path, strerror(errno));
        return -1;
    }
    
    // Step 4: Write all data with partial write handling
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t written = write(fd, (const char*)data + total_written, size - total_written);
        if (written < 0) {
            if (errno == EINTR) continue;  // Interrupted system call - retry
            fprintf(stderr, "ERROR: Write failed: %s\n", strerror(errno));
            close(fd);
            unlink(tmp_path);  // Clean up partial file
            return -1;
        }
        total_written += written;
    }
    
    printf("Written %zu bytes to tmp file\n", total_written);
    
    // Step 5: Force data to physical storage (durability)
    if (fsync(fd) != 0) {
        fprintf(stderr, "ERROR: fsync(%s) failed: %s\n", tmp_path, strerror(errno));
        close(fd);
        unlink(tmp_path);
        return -1;
    }
    
    printf("fsync completed for tmp file\n");
    
    // Step 6: Close temporary file
    if (close(fd) != 0) {
        fprintf(stderr, "ERROR: close(%s) failed: %s\n", tmp_path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    
    // Step 7: Atomic rename - this is the critical operation
    // After this point, either old file or new file exists, never a partial file
    if (rename(tmp_path, final_path) != 0) {
        fprintf(stderr, "ERROR: rename(%s → %s) failed: %s\n", tmp_path, final_path, strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    
    printf("Atomic rename completed: %s\n", final_path);
    
    // Step 8: Sync directory metadata to ensure rename is durable
    int dirfd = open(dir, O_DIRECTORY | O_RDONLY);
    if (dirfd >= 0) {
        if (fsync(dirfd) != 0) {
            fprintf(stderr, "WARNING: fsync(dir %s) failed: %s\n", dir, strerror(errno));
            // Don't return -1 here as file is already successfully renamed
        } else {
            printf("Directory fsync completed\n");
        }
        close(dirfd);
    } else {
        fprintf(stderr, "WARNING: Cannot open directory %s for fsync: %s\n", dir, strerror(errno));
    }
    
    printf("SUCCESS: JSON file saved atomically to %s\n", final_path);
    return 0;
}

/**
 * Wait for MQTT data from a specific node with timeout
 * This function blocks until data is available from the specified node
 * @param node_type: ID of the node to wait for data from
 * @param timeout_ms: Maximum time to wait in milliseconds
 * @return: 1 if data received within timeout, 0 if timeout occurred
 */
int wait_for_mqtt_data_by_node(int node_type, int timeout_ms) {
    node_config_t *node = get_node_by_id(node_type);
    if (!node || !node->mqtt_data) return 0;
    
    // Lock the node's MQTT data mutex
    pthread_mutex_lock(&node->mqtt_data->mutex);
    
    // Calculate absolute timeout time
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }
    
    // Wait for condition signal or timeout
    int result = pthread_cond_timedwait(&node->mqtt_data->cond, &node->mqtt_data->mutex, &timeout);
    pthread_mutex_unlock(&node->mqtt_data->mutex);
    return (result == 0) ? 1 : 0;
}

/**
 * Convert binary data to uppercase hexadecimal string representation
 * Each byte becomes two hex characters (e.g., 0xAB -> "AB")
 * @param data: Input binary data array
 * @param length: Number of bytes to convert
 * @param hex_str: Output buffer for hex string
 * @param hex_str_size: Size of output buffer
 */
void raw_data_to_hex_string(unsigned char *data, int length, char *hex_str, int hex_str_size) {
    if (hex_str_size < 1) return;
    int pos = 0;
    
    // Convert each byte to two hex characters
    for (int i = 0; i < length && pos + 3 <= hex_str_size; i++) {
        sprintf(hex_str + pos, "%02X", data[i]);
        pos += 2;
    }
    hex_str[pos] = '\0';  // Null terminate the string
}

/**
 * MQTT connection established callback
 * Called automatically when MQTT connection is successfully established
 * Publishes device attributes to inform server about gateway capabilities
 * @param mosq: MQTT client instance
 * @param userdata: User-defined data (unused)
 * @param result: Connection result code (0 = success)
 */
void on_mqtt_connect(struct mosquitto *mosq, void *userdata, int result) {
    if (result == 0) {
        mqtt_connected = 1;  // Set connection flag
        
        // Get configuration and system information
        mqtt_config_t *config = get_mqtt_config();
        system_info_t *sys_info = get_system_info();
        
        if (!config || !sys_info) return;
        
        // Allocate buffer for attributes JSON
        char *attributes = malloc(config->attributes_buffer_size);
        if (!attributes) return;
        
        // Build device attributes JSON with gateway information
        snprintf(attributes, config->attributes_buffer_size,
                "{"
                "\"gateway_ip\":\"%s\","           // Current IP address
                "\"firmware_version\":\"%s\","     // Firmware version
                "\"device_type\":\"%s\","          // Type of device
                "\"manufacturer\":\"%s\","         // Device manufacturer
                "\"model\":\"%s\","                // Device model
                "\"node_count\":%d"                // Number of connected nodes
                "}", 
                get_local_ip(), 
                sys_info->firmware_version,
                sys_info->device_type, 
                sys_info->manufacturer,
                sys_info->model,
                get_node_count());

        // Publish attributes to ThingsBoard
        mosquitto_publish(mqtt_client, NULL, config->topic_attributes,
                          strlen(attributes), attributes, config->qos, false);
        
        free(attributes);
    } else {
        mqtt_connected = 0;  // Connection failed
    }
}

/**
 * MQTT connection lost callback
 * Called when MQTT connection is unexpectedly lost
 * @param mosq: MQTT client instance
 * @param userdata: User-defined data (unused)
 * @param result: Disconnection reason code
 */
void on_mqtt_disconnect(struct mosquitto *mosq, void *userdata, int result) {
    mqtt_connected = 0;  // Clear connection flag
}

/**
 * MQTT message publish confirmation callback
 * Called when a published message has been successfully sent
 * @param mosq: MQTT client instance
 * @param userdata: User-defined data (unused)
 * @param mid: Message ID of the published message
 */
void on_mqtt_publish(struct mosquitto *mosq, void *userdata, int mid) {
    // Message published successfully
}

// ===== ROBUST MQTT MESSAGE HANDLER WITH COMPREHENSIVE ERROR HANDLING =====
/**
 * MQTT message received callback with production-grade error handling
 * Handles incoming JSON configuration files from ThingsBoard with full validation
 * and atomic file writing to prevent data corruption during power failures
 * @param mosq: MQTT client instance
 * @param userdata: User-defined data (unused)
 * @param message: Received MQTT message containing topic and payload
 */
void on_mqtt_message_robust(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *message) {
    if (!message || !message->topic) {
        fprintf(stderr, "ERROR: Invalid MQTT message\n");
        return;
    }
    
    printf("Received MQTT message on topic: %s\n", message->topic);
    
    // Handle JSON config from ThingsBoard attributes response
    if (strstr(message->topic, "v1/devices/me/attributes/response/")) {
        printf("Processing ThingsBoard attributes response...\n");
        
        const void *payload = message->payload;
        size_t payload_size = (size_t)message->payloadlen;
        
        // Step 1: Validate JSON data before processing
        if (!validate_json_basic(payload, payload_size)) {
            fprintf(stderr, "ERROR: JSON validation failed\n");
            return;
        }
        
        // Step 2: Log preview of received data for debugging
        size_t preview_len = payload_size > 200 ? 200 : payload_size;
        printf("JSON preview (%zu/%zu bytes): %.*s%s\n", 
               preview_len, payload_size,
               (int)preview_len, (char*)payload,
               payload_size > preview_len ? "..." : "");
        
        // Step 3: Atomic write with comprehensive error handling
        int result = atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, payload, payload_size);
        if (result == 0) {
            printf("✓ Config JSON successfully saved and ready to use\n");
        } else {
            fprintf(stderr, "✗ Failed to save config JSON\n");
        }
    }
    // Handle push updates from shared attributes
    else if (strcmp(message->topic, "v1/devices/me/attributes") == 0) {
        const void *payload = message->payload;
        size_t payload_size = (size_t)message->payloadlen;
        
        if (payload_size > 0 && payload != NULL && payload_size <= MAX_JSON_SIZE) {
            printf("Received shared attributes update, saving to config...\n");
            if (validate_json_basic(payload, payload_size)) {
                atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, payload, payload_size);
            }
        }
    }
    // Log unhandled topics for debugging
    else {
        printf("Ignoring message from topic: %s\n", message->topic);
    }
}

/**
 * Request configuration JSON from ThingsBoard with robust error handling
 * Sets up subscriptions and sends request for shared attributes containing config data
 * Includes comprehensive error checking at each step
 * @return: 1 on success, 0 on error
 */
int request_config_json_robust(void) {
    mqtt_config_t *cfg = get_mqtt_config();
    if (!cfg) {
        fprintf(stderr, "ERROR: Cannot get MQTT config\n");
        return 0;
    }
    
    if (!mqtt_client) {
        fprintf(stderr, "ERROR: MQTT client not initialized\n");
        return 0;
    }
    
    if (!mqtt_connected) {
        fprintf(stderr, "ERROR: MQTT not connected\n");
        return 0;
    }
    
    printf("Setting up JSON config request...\n");
    
    // Step 1: Set robust message callback
    mosquitto_message_callback_set(mqtt_client, on_mqtt_message_robust);
    printf("Message callback set\n");
    
    // Step 2: Subscribe to attributes response topic
    int rc = mosquitto_subscribe(mqtt_client, NULL, "v1/devices/me/attributes/response/+", cfg->qos);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "ERROR: Subscribe response failed: %s\n", mosquitto_strerror(rc));
        return 0;
    }
    printf("Subscribed to attributes response topic\n");
    
    // Step 3: Subscribe to push updates (optional)
    rc = mosquitto_subscribe(mqtt_client, NULL, "v1/devices/me/attributes", cfg->qos);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "WARNING: Subscribe push updates failed: %s\n", mosquitto_strerror(rc));
        // Don't return 0 as response subscription succeeded
    } else {
        printf("Subscribed to attributes push updates\n");
    }
    
    // Step 4: Send request for config data
    const char *request_payload = "{\"sharedKeys\":\"config\"}";
    rc = mosquitto_publish(mqtt_client, NULL, "v1/devices/me/attributes/request/1",
                           (int)strlen(request_payload), request_payload, cfg->qos, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "ERROR: Publish request failed: %s\n", mosquitto_strerror(rc));
        return 0;
    }
    
    printf("✓ Config JSON request sent successfully\n");
    printf("Waiting for ThingsBoard response...\n");
    return 1;
}

/**
 * Build JSON telemetry payload with raw data from all connected nodes
 * Collects data from all configured nodes and formats it as JSON for transmission
 * @param payload: Output buffer for JSON string
 * @param payload_size: Size of output buffer
 * @param timestamp: Unix timestamp to include in telemetry
 */
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp) {
    mqtt_config_t *config = get_mqtt_config();
    if (!config) return;
    
    // Allocate temporary buffer for building JSON parts
    char *temp_buffer = malloc(1024);
    if (!temp_buffer) return;
    
    // Start JSON object with optional timestamp
    if (config->system_fields.include_timestamp) {
        snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);  // Milliseconds
    } else {
        snprintf(payload, payload_size, "{");
    }

    // Add raw data from all configured nodes
    for (int i = 0; i < get_node_count(); i++) {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->mqtt_data) continue;

        // Lock node data to safely access it
        pthread_mutex_lock(&node->mqtt_data->mutex);
        if (node->mqtt_data->data) {
            raw_data_t *raw_data = (raw_data_t*)node->mqtt_data->data;
            
            if (raw_data && raw_data->data && raw_data->length > 0) {
                // Convert raw binary data to hex string representation
                char *hex_str = malloc(raw_data->length * 2 + 1);
                if (hex_str) {
                    raw_data_to_hex_string(raw_data->data, raw_data->length, hex_str, raw_data->length * 2 + 1);
                    
                    // Add node data to JSON payload
                    snprintf(temp_buffer, 1024, ",\"node%d_raw_data\":\"%s\",\"node%d_data_length\":%d", 
                             node->node_id, hex_str, node->node_id, raw_data->length);
                    
                    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
                    free(hex_str);
                }
            }
        }
        pthread_mutex_unlock(&node->mqtt_data->mutex);
    }

    // Add optional system information fields based on configuration
    if (config->system_fields.include_gateway_ip) {
        snprintf(temp_buffer, 1024, ",\"gateway_ip\":\"%s\"", get_local_ip());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    
    if (config->system_fields.data_source[0] != '\0') {
        snprintf(temp_buffer, 1024, ",\"data_source\":\"%s\"", config->system_fields.data_source);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    
    if (config->system_fields.include_node_count) {
        snprintf(temp_buffer, 1024, ",\"node_count\":%d", get_node_count());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    
    // Close JSON object
    strncat(payload, "}", payload_size - strlen(payload) - 1);
    free(temp_buffer);
}

/**
 * Main MQTT thread function with robust config download capability
 * Handles MQTT connection, requests JSON config, and publishes telemetry data periodically
 * This thread runs continuously throughout the application lifetime
 * @param arg: Thread arguments (unused)
 * @return: NULL when thread terminates
 */
void *mqtt_thread_func(void *arg) {
    // Get MQTT configuration from loaded config file
    mqtt_config_t *config = get_mqtt_config();
    if (!config) return NULL;
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    // Create new MQTT client instance
    mqtt_client = mosquitto_new(config->client_id, true, NULL);
    if (!mqtt_client) return NULL;

    // Set authentication credentials (username/password or access token)
    mosquitto_username_pw_set(mqtt_client, config->username, config->password);
    
    // Set callback functions for MQTT events
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);

    // Attempt to connect to MQTT broker
    int rc = mosquitto_connect(mqtt_client, config->broker_host, config->broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

    // Start the network loop in a separate thread
    mosquitto_loop_start(mqtt_client);

    // Wait for connection establishment with timeout
    int connection_timeout = config->connection_timeout;
    while (!mqtt_connected && connection_timeout > 0) {
        usleep(100 * 1000);  // Sleep 100ms
        connection_timeout--;
    }

    // If connection failed within timeout, cleanup and exit
    if (!mqtt_connected) {
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

    // ===== REQUEST CONFIG JSON WITH ROBUST ERROR HANDLING =====
    printf("=== Starting robust config JSON download ===\n");
    if (request_config_json_robust()) {
        printf("Config request initiated successfully\n");
        // Optional: wait a moment for response to arrive
        // sleep(2);
    } else {
        fprintf(stderr, "Config request failed, continuing with default config\n");
    }
    printf("=== Config download setup completed ===\n");

    // Initialize telemetry publishing variables
    time_t last_publish = 0;
    char *telemetry_payload = malloc(config->payload_buffer_size);
    if (!telemetry_payload) {
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }
    
    // Main loop - publishes telemetry data at configured intervals
    while (1) {
        time_t current_time = time(NULL);
        
        // Check if it's time to publish telemetry data
        if (current_time - last_publish >= config->publish_interval) {
            // Build JSON payload with current node data
            build_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);

            // Publish telemetry data to ThingsBoard
            rc = mosquitto_publish(mqtt_client, NULL, config->topic_telemetry,
                                   strlen(telemetry_payload), telemetry_payload, 
                                   config->qos, false);
            if (rc == MOSQ_ERR_SUCCESS) {
                last_publish = current_time;
            }
        }

        // Handle disconnection - attempt to reconnect
        if (!mqtt_connected) {
            mosquitto_reconnect(mqtt_client);
            usleep(config->reconnect_delay_ms * 1000);
        }

        // Sleep before next iteration
        usleep(config->loop_interval_ms * 1000);
    }

    // Cleanup resources before thread termination
    free(telemetry_payload);
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mosquitto_lib_cleanup();
    return NULL;
}
