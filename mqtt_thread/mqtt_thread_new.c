#include "mongoose.h"
#include "thread_func.h"

// Config update tracking
static volatile int config_updated = 0;
static pthread_mutex_t config_update_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global Mongoose manager and connection
static struct mg_mgr mqtt_mgr;
static struct mg_connection *mqtt_connection = NULL;
volatile int mqtt_connected = 0;

thread_pause_t mqtt_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};

// Safe MQTT config access with mutex protection
mqtt_config_t *safe_get_mqtt_config(void)
{
    pthread_mutex_lock(&config_mutex);
    if (config_reloading)
    {
        pthread_mutex_unlock(&config_mutex);
        return NULL;
    }
    mqtt_config_t *config = get_mqtt_config();
    pthread_mutex_unlock(&config_mutex);
    return config;
}

// Get local IP address for gateway identification
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

// Ensure directory exists for config files
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

// Validate basic JSON structure
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

// Atomic file write with durability guarantees
static int atomic_write_json_file(const char *dir, const char *filename, const void *data, size_t size)
{
    if (ensure_directory_exists(dir) != 0)
    {
        return -1;
    }

    char current_path[64], new_path[64], backup_path[64];
    snprintf(current_path, sizeof(current_path), "%s/%s", dir, filename);
    snprintf(new_path, sizeof(new_path), "%s/%s.new", dir, filename);
    snprintf(backup_path, sizeof(backup_path), "%s/config_backup.json", dir);

    // Delete old config file if it exists
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

    // Create new file
    int fd = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: Cannot create new file %s: %s\n",
                new_path, strerror(errno));
#endif
        return -1;
    }

    // Write data to new file with partial-write handling
    size_t total_written = 0;
    while (total_written < size)
    {
        ssize_t written = write(fd, (const char *)data + total_written,
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
            unlink(new_path);
            return -1;
        }
        total_written += written;
    }

    // Ensure data is flushed to storage
    if (fsync(fd) != 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: fsync new file failed: %s\n", strerror(errno));
#endif
        close(fd);
        unlink(new_path);
        return -1;
    }

    close(fd);

    // Atomically rename new file to current filename
    if (rename(new_path, current_path) != 0)
    {
#ifdef DEBUG
        fprintf(stderr, "ERROR: rename new file failed: %s\n", strerror(errno));
#endif
        unlink(new_path);
        return -1;
    }

#ifdef DEBUG
    printf("New config file created successfully: %s\n", current_path);
#endif
    return 0;
}

// Process control command from server
static void process_control_command(const char *payload)
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
                printf("Server command: node=%d, cmd=%d, params=%s\n",
                       node_id, cmd_id, params_str);
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

// MQTT event handler for Mongoose
static void mqtt_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    switch (ev)
    {
    case MG_EV_MQTT_OPEN:
    {
        // Connected to the broker
        mqtt_connected = 1;
#ifdef DEBUG
        printf("MQTT connected successfully\n");
#endif

        mqtt_config_t *config = get_mqtt_config();
        system_info_t *sys_info = get_system_info();
        if (!config || !sys_info)
            return;

        // Subscribe to control topic
        if (strlen(config->topic_control) > 0)
        {
            struct mg_mqtt_opts sub_opts = {0};
            sub_opts.topic = mg_str(config->topic_control);
            sub_opts.qos = config->qos;
            mg_mqtt_sub(c, &sub_opts);
#ifdef DEBUG
            printf("Subscribed to control topic: %s\n", config->topic_control);
#endif
        }

        // Subscribe to RPC request topic
        struct mg_mqtt_opts rpc_opts = {0};
        rpc_opts.topic = mg_str("v1/devices/me/rpc/request/+");
        rpc_opts.qos = config->qos;
        mg_mqtt_sub(c, &rpc_opts);
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

            struct mg_mqtt_opts pub_opts = {0};
            pub_opts.topic = mg_str(config->topic_attributes);
            pub_opts.message = mg_str(attributes);
            pub_opts.qos = config->qos;
            pub_opts.retain = false;
            mg_mqtt_pub(c, &pub_opts);
            free(attributes);
        }
        break;
    }

    case MG_EV_MQTT_MSG:
    {
        // Received MQTT message event
        struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
#ifdef DEBUG
        printf("MQTT message on topic: %.*s\n", (int)mm->topic.len, mm->topic.buf);
#endif

        // Protect config with mutex
        pthread_mutex_lock(&config_mutex);
        if (config_reloading)
        {
            pthread_mutex_unlock(&config_mutex);
            return;
        }

        mqtt_config_t *config = get_mqtt_config();
        if (!config)
        {
            pthread_mutex_unlock(&config_mutex);
            return;
        }

        // Copy topic string safely
        char topic_str[256] = {0};
        int topic_len = (mm->topic.len < 255) ? mm->topic.len : 255;
        memcpy(topic_str, mm->topic.buf, topic_len);
        topic_str[topic_len] = '\0';

        char topic_control[256] = {0};
        if (config->topic_control && strlen(config->topic_control) > 0)
        {
            strncpy(topic_control, config->topic_control, sizeof(topic_control) - 1);
        }
        pthread_mutex_unlock(&config_mutex);

        // Check if message is from control topics
        if ((strlen(topic_control) > 0 && strstr(topic_str, topic_control)) ||
            strstr(topic_str, "v1/devices/me/rpc/request/"))
        {
#ifdef DEBUG
            printf("Processing control command\n");
#endif

            // Process payload
            if (mm->data.len > 0)
            {
                char *payload_str = malloc(mm->data.len + 1);
                if (payload_str)
                {
                    memcpy(payload_str, mm->data.buf, mm->data.len);
                    payload_str[mm->data.len] = '\0';
                    process_control_command(payload_str);
                    free(payload_str);
                }
            }
            return;
        }

        // Check for config update topics
        if (strstr(topic_str, "v1/devices/me/attributes/response/") ||
            strcmp(topic_str, "v1/devices/me/attributes") == 0)
        {
#ifdef DEBUG
            printf("Processing config update\n");
#endif

            const void *payload = mm->data.buf;
            size_t payload_size = mm->data.len;

            if (!validate_json_basic(payload, payload_size))
            {
#ifdef DEBUG
                fprintf(stderr, "ERROR: JSON validation failed\n");
#endif
                return;
            }

            int result = atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, payload, payload_size);
            if (result == 0)
            {
#ifdef DEBUG
                printf("Config updated from server\n");
#endif
                if (pthread_mutex_trylock(&config_update_mutex) == 0)
                {
                    config_updated = 1;
                    pthread_mutex_unlock(&config_update_mutex);
                }
            }
        }
        break;
    }

    case MG_EV_CLOSE:
    {
        // Connection closed
        mqtt_connected = 0;
        mqtt_connection = NULL;
#ifdef DEBUG
        printf("MQTT disconnected\n");
#endif
        break;
    }

    case MG_EV_ERROR:
    {
        // Connection error
        mqtt_connected = 0;
        mqtt_connection = NULL;
#ifdef DEBUG
        printf("MQTT connection error\n");
#endif
        break;
    }
    }
}

// Check and reload config if updated
static int check_and_reload_config(void)
{
    pthread_mutex_lock(&config_update_mutex);
    int should_reload = config_updated;
    if (should_reload)
    {
        config_updated = 0;
    }
    pthread_mutex_unlock(&config_update_mutex);

    if (!should_reload)
        return 0;

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

// Request config from server
static int request_config_json_robust(void)
{
    pthread_mutex_lock(&config_mutex);
    mqtt_config_t *cfg = get_mqtt_config();
    if (!cfg || !mqtt_connection || !mqtt_connected)
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

    // Subscribe to config topics
    struct mg_mqtt_opts attr_resp_opts = {0};
    attr_resp_opts.topic = mg_str("v1/devices/me/attributes/response/+");
    attr_resp_opts.qos = cfg->qos;
    mg_mqtt_sub(mqtt_connection, &attr_resp_opts);

    struct mg_mqtt_opts attr_opts = {0};
    attr_opts.topic = mg_str("v1/devices/me/attributes");
    attr_opts.qos = cfg->qos;
    mg_mqtt_sub(mqtt_connection, &attr_opts);

    // Publish config request
    const char *request_payload = "{\"sharedKeys\":\"config\"}";
    struct mg_mqtt_opts pub_opts = {0};
    pub_opts.topic = mg_str("v1/devices/me/attributes/request/1");
    pub_opts.message = mg_str(request_payload);
    pub_opts.qos = cfg->qos;
    pub_opts.retain = false;
    mg_mqtt_pub(mqtt_connection, &pub_opts);

#ifdef DEBUG
    printf("Config request sent\n");
#endif

    pthread_mutex_unlock(&config_mutex);
    return 1;
}

// Build telemetry payload with node data
static void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp)
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

    // Add data from detected nodes - no config mutex lock
    for (int i = 0; i < get_node_count(); i++)
    {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->mqtt_data)
            continue;

        // Only trylock node mutex
        if (pthread_mutex_trylock(&node->mqtt_data->mutex) == 0)
        {
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
        // If trylock fails, skip this node
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

// MQTT main thread function
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

    // Init Mongoose manager
    mg_mgr_init(&mqtt_mgr);
    // Build Connection URL
    char url[512];
    if (strlen(config->username) > 0)
    {
        snprintf(url, sizeof(url), "mqtt://%s@%s:%d",
                 config->username, config->broker_host, config->broker_port);
    }
    else
    {
        snprintf(url, sizeof(url), "mqtt://%s:%d",
                 config->broker_host, config->broker_port);
    }

#ifdef DEBUG
    printf("Connecting to MQTT broker: %s\n", url);
    printf("DEBUG: Username: '%s'\n", config->username);
    printf("DEBUG: Password: '%s'\n", config->password);
#endif

    struct mg_mqtt_opts opts = {
        .client_id = mg_str(config->client_id),
        .user = mg_str(config->username), // Explicit username
        .pass = mg_str(""),               // Empty password for ThingsBoard
        .clean = true,                    // Clean session
        .keepalive = 60,                  // Keep alive timeout
        .version = 4                      // MQTT 3.1.1
    };

    // Connect to MQTT
    mqtt_connection = mg_mqtt_connect(&mqtt_mgr, url, &opts, mqtt_event_handler, NULL);
    if (!mqtt_connection)
    {
#ifdef DEBUG
        printf("Failed to create MQTT connection\n");
#endif
        mg_mgr_free(&mqtt_mgr);
        return NULL;
    }

    // Wait for connection
    int connection_timeout = config->connection_timeout;
    while (!mqtt_connected && connection_timeout > 0)
    {
        mg_mgr_poll(&mqtt_mgr, 100); // 100 ms poll
        connection_timeout--;

        if (connection_timeout % 10 == 0 && connection_timeout > 0)
        {
#ifdef DEBUG
            printf("MQTT: Waiting for connection... (%d seconds left)\n", connection_timeout / 10);
#endif
        }
    }

    if (!mqtt_connected)
    {
#ifdef DEBUG
        printf("MQTT connection timeout\n");
#endif
        mg_mgr_free(&mqtt_mgr);
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

    // Allocate buffers
    time_t last_publish = 0, last_status = 0;
    char *telemetry_payload = malloc(config->payload_buffer_size);
    char *status_payload = malloc(config->payload_buffer_size);
    if (!telemetry_payload || !status_payload)
    {
#ifdef DEBUG
        printf("Memory allocation failed\n");
#endif
        mg_mgr_free(&mqtt_mgr);
        return NULL;
    }

    while (1)
    {
        // Pause thread if needed
        pthread_mutex_lock(&mqtt_pause.mutex);
        while (mqtt_pause.is_paused)
        {
            pthread_cond_wait(&mqtt_pause.cond, &mqtt_pause.mutex);
        }
        pthread_mutex_unlock(&mqtt_pause.mutex);

        // Poll events
        mg_mgr_poll(&mqtt_mgr, 100);

        time_t current_time = time(NULL);

        // Reload config if updated
        check_and_reload_config();

        // Publish telemetry at interval
        if (current_time - last_publish >= config->publish_interval)
        {
            build_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);
            if (mqtt_connection && mqtt_connected)
            {
                struct mg_mqtt_opts pub_opts = {0};
                pub_opts.topic = mg_str(config->topic_telemetry);
                pub_opts.message = mg_str(telemetry_payload);
                pub_opts.qos = config->qos;
                pub_opts.retain = false;
                mg_mqtt_pub(mqtt_connection, &pub_opts);
                last_publish = current_time;

#ifdef DEBUG
                printf("Telemetry published: %s\n", telemetry_payload);
#endif
            }
        }

        // Send status update every 60 seconds
        if (strlen(config->topic_status) > 0 && (current_time - last_status) >= 60)
        {
            snprintf(status_payload, config->payload_buffer_size,
                     "{\"status\":\"online\",\"timestamp\":%ld000,\"detected_nodes\":%d}",
                     current_time, get_node_count());
            if (mqtt_connection && mqtt_connected)
            {
                struct mg_mqtt_opts pub_opts = {0};
                pub_opts.topic = mg_str(config->topic_status);
                pub_opts.message = mg_str(status_payload);
                pub_opts.qos = config->qos;
                pub_opts.retain = false;
                mg_mqtt_pub(mqtt_connection, &pub_opts);
                last_status = current_time;
            }
        }

        if (!mqtt_connected)
        {
#ifdef DEBUG
            printf("MQTT: Connection lost, attempting reconnect...\n");
#endif

            // Clear existing connection
            if (mqtt_connection)
            {
                mg_mgr_poll(&mqtt_mgr, 0); // Process any pending events
                mqtt_connection = NULL;
            }

            // Create new connection
            mqtt_connection = mg_mqtt_connect(&mqtt_mgr, url, &opts, mqtt_event_handler, NULL);

            // Wait before next attempt
            usleep(5000 * 1000); // 5 seconds delay
        }

        usleep(config->loop_interval_ms * 1000);
    }

    // Cleanup
    free(telemetry_payload);
    free(status_payload);
    mg_mgr_free(&mqtt_mgr);
    return NULL;
}

/**
 * Update MQTT data with received response
 */
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len)
{
    if (!node || !node->mqtt_data || !resp || resp_len == 0)
        return;

    pthread_mutex_lock(&node->mqtt_data->mutex);

    // Free old data if exists
    if (node->mqtt_data->data)
    {
        raw_data_t *old_data = (raw_data_t *)node->mqtt_data->data;
        if (old_data->data)
        {
            free(old_data->data);
        }
        free(old_data);
    }

    // Store new raw data
    raw_data_t *raw_data = malloc(sizeof(raw_data_t));
    if (raw_data)
    {
        raw_data->length = resp_len;
        raw_data->timestamp = time(NULL); // ADD timestamp
        raw_data->data = malloc(resp_len);
        if (raw_data->data)
        {
            memcpy(raw_data->data, resp, resp_len);
            node->mqtt_data->data = raw_data;
            pthread_cond_signal(&node->mqtt_data->cond);
        }
        else
        {
            free(raw_data);
        }
    }

    pthread_mutex_unlock(&node->mqtt_data->mutex);
}
