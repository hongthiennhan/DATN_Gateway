#include "main.h"
#include "node_config.h"

// Global node registry
static node_registry_t node_registry = {0};
// Global config protection
pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile uint32_t config_reloading = 0;

/**
 * Safe string copy with guaranteed null termination
 */
static void safe_strncpy(char *dest, const char *src, size_t dest_size)
{
    if (!dest || !src || dest_size == 0)
        return;
    size_t len = strlen(src);
    if (len >= dest_size)
        len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = '\0'; // Ensure null terminator
}

/**
 * Allocate shared data structure
 */
static shared_data_t *allocate_shared_data(void)
{
    shared_data_t *shared = malloc(sizeof(shared_data_t));
    if (!shared)
        return NULL;
    shared->data = NULL;
    if (pthread_mutex_init(&shared->mutex, NULL) != 0)
    {
        free(shared);
        return NULL;
    }
    if (pthread_cond_init(&shared->cond, NULL) != 0)
    {
        pthread_mutex_destroy(&shared->mutex);
        free(shared);
        return NULL;
    }
    return shared;
}

/**
 * Free shared data structure
 */
static void free_shared_data(shared_data_t *shared)
{
    if (!shared)
        return;
    pthread_mutex_destroy(&shared->mutex);
    pthread_cond_destroy(&shared->cond);
    if (shared->data)
    {
        free(shared->data);
    }
    free(shared);
}

/**
 * Parse detection commands from JSON
 */
static uint32_t parse_detection_commands(json_object *detection_array, node_config_t *node)
{
    if (!detection_array || !node)
        return -1;
    uint32_t array_len = json_object_array_length(detection_array);
    if (array_len <= 0)
        return 0;
    node->detection_commands = malloc(sizeof(detection_cmd_t) * array_len);
    if (!node->detection_commands)
        return -1;
    node->detection_count = array_len;
    for (uint32_t i = 0; i < array_len; i++)
    {
        json_object *cmd_obj = json_object_array_get_idx(detection_array, i);
        detection_cmd_t *cmd = &node->detection_commands[i];
        json_object *temp_obj;
        memset(cmd, 0, sizeof(detection_cmd_t));
        if (json_object_object_get_ex(cmd_obj, "command", &temp_obj))
        {
            safe_strncpy(cmd->command, json_object_get_string(temp_obj),
                         sizeof(cmd->command));
        }
        if (json_object_object_get_ex(cmd_obj, "expected_response", &temp_obj))
        {
            safe_strncpy(cmd->expected_response, json_object_get_string(temp_obj),
                         sizeof(cmd->expected_response));
        }
        if (json_object_object_get_ex(cmd_obj, "timeout_ms", &temp_obj))
        {
            cmd->timeout_ms = json_object_get_int(temp_obj);
        }
        if (json_object_object_get_ex(cmd_obj, "description", &temp_obj))
        {
            safe_strncpy(cmd->description, json_object_get_string(temp_obj),
                         sizeof(cmd->description));
        }
    }
    return 0;
}

/**
 * Parse menu items for node
 */
static uint32_t parse_menu_items(json_object *menu_array, node_config_t *node)
{
    if (!menu_array || !node)
        return -1;
    uint32_t array_len = json_object_array_length(menu_array);
    if (array_len <= 0)
        return 0;
    node->menu_items = malloc(sizeof(menu_item_t) * array_len);
    if (!node->menu_items)
        return -1;
    node->menu_count = array_len;
    for (uint32_t i = 0; i < array_len; i++)
    {
        json_object *item_obj = json_object_array_get_idx(menu_array, i);
        menu_item_t *item = &node->menu_items[i];
        json_object *temp_obj;
        memset(item, 0, sizeof(menu_item_t));
        if (json_object_object_get_ex(item_obj, "cmd", &temp_obj))
        {
            item->cmd = json_object_get_int(temp_obj);
        }
        if (json_object_object_get_ex(item_obj, "label", &temp_obj))
        {
            safe_strncpy(item->label, json_object_get_string(temp_obj), sizeof(item->label));
        }
        if (json_object_object_get_ex(item_obj, "hex_value", &temp_obj))
        {
            safe_strncpy(item->hex_value, json_object_get_string(temp_obj), sizeof(item->hex_value));
        }
        if (json_object_object_get_ex(item_obj, "timeout_ms", &temp_obj))
        {
            item->timeout_ms = json_object_get_int(temp_obj);
        }
    }
    return 0;
}

/**
 * Parse individual node configuration
 */
static uint32_t parse_node_config(json_object *node_obj, node_config_t *node)
{
    if (!node_obj || !node)
        return -1;
    json_object *temp_obj;
    memset(node, 0, sizeof(node_config_t));
    // Parse basic node info
    if (json_object_object_get_ex(node_obj, "id", &temp_obj))
    {
        node->node_id = json_object_get_int(temp_obj);
    }
    if (json_object_object_get_ex(node_obj, "name", &temp_obj))
    {
        safe_strncpy(node->name, json_object_get_string(temp_obj), sizeof(node->name));
    }
    if (json_object_object_get_ex(node_obj, "com_type", &temp_obj))
    {
        safe_strncpy(node->com_type, json_object_get_string(temp_obj), sizeof(node->com_type));
    }
    if (json_object_object_get_ex(node_obj, "address", &temp_obj))
    {
        safe_strncpy(node->address, json_object_get_string(temp_obj), sizeof(node->address));
    }
    if (json_object_object_get_ex(node_obj, "data_format", &temp_obj))
    {
        safe_strncpy(node->data_format, json_object_get_string(temp_obj), sizeof(node->data_format));
    }
    if (json_object_object_get_ex(node_obj, "reflash_script", &temp_obj))
    {
        safe_strncpy(node->reflash_script, json_object_get_string(temp_obj), sizeof(node->reflash_script));
    }

    // Parse detection commands
    json_object *detection_array;
    if (json_object_object_get_ex(node_obj, "detection_commands", &detection_array))
    {
        parse_detection_commands(detection_array, node);
    }

    // Parse menu items
    json_object *menu_array;
    if (json_object_object_get_ex(node_obj, "menu_items", &menu_array))
    {
        parse_menu_items(menu_array, node);
    }

    // Initialize runtime data
    node->detected = 0;
    node->last_detection = 0;
    node->mqtt_data = allocate_shared_data();
    node->tcp_data = allocate_shared_data();
    node->udp_data = allocate_shared_data();
    return 0;
}

/**
 * Load complete configuration from JSON file
 */
int load_nodes_config(const char *config_file)
{
#ifdef DEBUG
    printf("Loading configuration from: %s\n", config_file);
#endif
    json_object *root = json_object_from_file(config_file);
    if (!root)
    {
#ifdef DEBUG
        printf("Error: Cannot load config file %s\n", config_file);
#endif
        return -1;
    }

    // Initialize registry - ZERO OUT ALL MEMORY
    memset(&node_registry, 0, sizeof(node_registry));
    // Initialize control queue
    node_registry.communication_type = COMM_TYPE_MQTT;
    node_registry.control_queue.head = 0;
    node_registry.control_queue.tail = 0;
    node_registry.control_queue.count = 0;
    pthread_mutex_init(&node_registry.control_queue.mutex, NULL);
    pthread_cond_init(&node_registry.control_queue.cond, NULL);

    // Handle ThingsBoard wrapper format
    json_object *effective_root = root;
    json_object *shared_obj, *config_obj;
    if (json_object_object_get_ex(root, "shared", &shared_obj) &&
        json_object_object_get_ex(shared_obj, "config", &config_obj))
    {
        effective_root = config_obj;
#ifdef DEBUG
        printf("Using ThingsBoard wrapper format\n");
#endif
    }
    if (json_object_object_get_ex(root, "config", &config_obj))
    {
        effective_root = config_obj;
#ifdef DEBUG
        printf("Using ThingsBoard wrapper format v2\n");
#endif
    }

    // Parse system configuration
    json_object *system_obj;
    if (json_object_object_get_ex(effective_root, "system_config", &system_obj))
    {
        json_object *temp_obj;
        if (json_object_object_get_ex(system_obj, "ui_refresh_delay", &temp_obj))
            node_registry.ui_refresh_delay = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "default_baudrate", &temp_obj))
            node_registry.default_baudrate = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "default_device", &temp_obj))
        {
            safe_strncpy(node_registry.default_device, json_object_get_string(temp_obj),
                         sizeof(node_registry.default_device));
        }
        if (json_object_object_get_ex(system_obj, "startup_clear_duration", &temp_obj))
            node_registry.startup_clear_duration = json_object_get_int(temp_obj);
        // Parse detection settings
    }

    // Parse system info
    json_object *system_info_obj;
    if (json_object_object_get_ex(effective_root, "system_info", &system_info_obj))
    {
        json_object *temp_obj;
        if (json_object_object_get_ex(system_info_obj, "firmware_version", &temp_obj))
        {
            safe_strncpy(node_registry.system_info.firmware_version,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.system_info.firmware_version));
        }
        if (json_object_object_get_ex(system_info_obj, "device_type", &temp_obj))
        {
            safe_strncpy(node_registry.system_info.device_type,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.system_info.device_type));
        }
        if (json_object_object_get_ex(system_info_obj, "manufacturer", &temp_obj))
        {
            safe_strncpy(node_registry.system_info.manufacturer,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.system_info.manufacturer));
        }
        if (json_object_object_get_ex(system_info_obj, "model", &temp_obj))
        {
            safe_strncpy(node_registry.system_info.model,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.system_info.model));
        }
    }

    // Parse UART config (abbreviated for space)
    json_object *uart_obj;
    if (json_object_object_get_ex(effective_root, "uart_config", &uart_obj))
    {
        json_object *temp_obj;
        // Parse nested buffer_sizes
        json_object *buffer_sizes_obj;
        if (json_object_object_get_ex(uart_obj, "buffer_sizes", &buffer_sizes_obj))
        {
            if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer", &temp_obj))
                node_registry.uart_config.response_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
                node_registry.uart_config.temp_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer", &temp_obj))
                node_registry.uart_config.error_message_buffer_size = json_object_get_int(temp_obj);
        }

        // Parse nested timing
        json_object *timing_obj;
        if (json_object_object_get_ex(uart_obj, "timing", &timing_obj))
        {
            if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &temp_obj))
                node_registry.uart_config.poll_interval_ms = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(timing_obj, "flush_interval_ms", &temp_obj))
                node_registry.uart_config.flush_interval_ms = json_object_get_int(temp_obj);
        }
        json_object *support_baudrate;
        // add support baudrate here:
        if (json_object_object_get_ex(uart_obj, "supported_baudrates", &support_baudrate))
        {
            uint32_t baudrate_array_len = json_object_array_length(support_baudrate);
            if (baudrate_array_len > 0)
            {
                node_registry.uart_config.supported_baudrates = malloc(sizeof(baudrate_mapping_t) * baudrate_array_len);
                if (node_registry.uart_config.supported_baudrates)
                {
                    node_registry.uart_config.baudrate_count = 0;

                    for (uint32_t i = 0; i < baudrate_array_len; i++)
                    {
                        json_object *baudrate_obj = json_object_array_get_idx(support_baudrate, i);
                        json_object *rate_obj, *code_obj;

                        if (json_object_object_get_ex(baudrate_obj, "rate", &rate_obj) &&
                            json_object_object_get_ex(baudrate_obj, "speed_code", &code_obj))
                        {

                            baudrate_mapping_t *mapping = &node_registry.uart_config.supported_baudrates[node_registry.uart_config.baudrate_count];
                            mapping->rate = json_object_get_int(rate_obj);
                            safe_strncpy(mapping->speed_code, json_object_get_string(code_obj), sizeof(mapping->speed_code));

                            node_registry.uart_config.baudrate_count++;

#ifdef DEBUG
                            printf("Loaded baudrate: %d (%s)\n", mapping->rate, mapping->speed_code);
#endif
                        }
                    }

#ifdef DEBUG
                    printf("Total supported baudrates loaded: %d\n", node_registry.uart_config.baudrate_count);
#endif
                }
                else
                {
#ifdef DEBUG
                    printf("Error: Failed to allocate memory for supported baudrates\n");
#endif
                }
            }
        }
        if (json_object_object_get_ex(uart_obj, "default_baudrate_fallback", &temp_obj))
            node_registry.uart_config.default_baudrate_fallback = json_object_get_int(temp_obj);
    }
    // Parse Modbus config
    json_object *modbus_obj;
    if (json_object_object_get_ex(effective_root, "modbus_config", &modbus_obj))
    {
        json_object *temp_obj;
        // Parse nested buffer_sizes
        json_object *buffer_sizes_obj;
        if (json_object_object_get_ex(modbus_obj, "buffer_sizes", &buffer_sizes_obj))
        {
            if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer", &temp_obj))
                node_registry.modbus_config.response_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
                node_registry.modbus_config.temp_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer", &temp_obj))
                node_registry.modbus_config.error_message_buffer_size = json_object_get_int(temp_obj);
        }

        // Parse nested timing
        json_object *timing_obj;
        if (json_object_object_get_ex(modbus_obj, "timing", &timing_obj))
        {
            if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &temp_obj))
                node_registry.modbus_config.poll_interval_ms = json_object_get_int(temp_obj);
        }

        json_object *support_baudrate;
        if (json_object_object_get_ex(modbus_obj, "supported_baudrates", &support_baudrate))
        {
            uint32_t baudrate_array_len = json_object_array_length(support_baudrate);
            if (baudrate_array_len > 0)
            {
                node_registry.modbus_config.supported_baudrates = malloc(sizeof(baudrate_mapping_t) * baudrate_array_len);
                if (node_registry.modbus_config.supported_baudrates)
                {
                    node_registry.modbus_config.baudrate_count = 0;

                    for (uint32_t i = 0; i < baudrate_array_len; i++)
                    {
                        json_object *baudrate_obj = json_object_array_get_idx(support_baudrate, i);
                        json_object *rate_obj, *code_obj;

                        if (json_object_object_get_ex(baudrate_obj, "rate", &rate_obj) &&
                            json_object_object_get_ex(baudrate_obj, "speed_code", &code_obj))
                        {

                            baudrate_mapping_t *mapping = &node_registry.modbus_config.supported_baudrates[node_registry.modbus_config.baudrate_count];
                            mapping->rate = json_object_get_int(rate_obj);
                            safe_strncpy(mapping->speed_code, json_object_get_string(code_obj), sizeof(mapping->speed_code));

                            node_registry.modbus_config.baudrate_count++;

#ifdef DEBUG
                            printf("Loaded modbus baudrate: %d (%s)\n", mapping->rate, mapping->speed_code);
#endif
                        }
                    }

#ifdef DEBUG
                    printf("Total modbus supported baudrates loaded: %d\n", node_registry.modbus_config.baudrate_count);
#endif
                }
                else
                {
#ifdef DEBUG
                    printf("Error: Failed to allocate memory for modbus supported baudrates\n");
#endif
                }
            }
        }

        if (json_object_object_get_ex(modbus_obj, "default_baudrate_fallback", &temp_obj))
            node_registry.modbus_config.default_baudrate_fallback = json_object_get_int(temp_obj);
    }

    // Parse CAN config:
    json_object *can_obj;
    if (json_object_object_get_ex(effective_root, "can_config", &can_obj))
    {
        json_object *temp_obj;
        json_object *buffer_sizes_obj;

        if (json_object_object_get_ex(can_obj, "buffer_sizes", &buffer_sizes_obj))
        {
            if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer", &temp_obj))
                node_registry.can_config.response_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
                node_registry.can_config.temp_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer", &temp_obj))
                node_registry.can_config.error_message_buffer_size = json_object_get_int(temp_obj);
        }

        json_object *timing_obj;
        if (json_object_object_get_ex(can_obj, "timing", &timing_obj))
        {
            if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &temp_obj))
                node_registry.can_config.poll_interval_ms = json_object_get_int(temp_obj);
        }

        json_object *support_baudrate;
        if (json_object_object_get_ex(can_obj, "supported_baudrates", &support_baudrate))
        {
            uint32_t baudrate_array_len = json_object_array_length(support_baudrate);
            if (baudrate_array_len > 0)
            {
                node_registry.can_config.supported_baudrates = malloc(sizeof(baudrate_mapping_t) * baudrate_array_len);
                if (node_registry.can_config.supported_baudrates)
                {
                    node_registry.can_config.baudrate_count = 0;

                    for (uint32_t i = 0; i < baudrate_array_len; i++)
                    {
                        json_object *baudrate_obj = json_object_array_get_idx(support_baudrate, i);
                        json_object *rate_obj, *code_obj;

                        if (json_object_object_get_ex(baudrate_obj, "rate", &rate_obj) &&
                            json_object_object_get_ex(baudrate_obj, "speed_code", &code_obj))
                        {

                            baudrate_mapping_t *mapping = &node_registry.can_config.supported_baudrates[node_registry.can_config.baudrate_count];
                            mapping->rate = json_object_get_int(rate_obj);
                            safe_strncpy(mapping->speed_code, json_object_get_string(code_obj), sizeof(mapping->speed_code));

                            node_registry.can_config.baudrate_count++;

#ifdef DEBUG
                            printf("Loaded can baudrate: %d (%s)\n", mapping->rate, mapping->speed_code);
#endif
                        }
                    }

#ifdef DEBUG
                    printf("Total can supported baudrates loaded: %d\n", node_registry.can_config.baudrate_count);
#endif
                }
                else
                {
#ifdef DEBUG
                    printf("Error: Failed to allocate memory for can supported baudrates\n");
#endif
                }
            }
        }

        if (json_object_object_get_ex(can_obj, "default_baudrate_fallback", &temp_obj))
            node_registry.can_config.default_baudrate_fallback = json_object_get_int(temp_obj);
    }

    // Parse MQTT config - SAFE VERSION
    json_object *mqtt_obj;
    if (json_object_object_get_ex(effective_root, "mqtt_config", &mqtt_obj))
    {
        json_object *temp_obj;
        if (json_object_object_get_ex(mqtt_obj, "broker_host", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.broker_host,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.broker_host));
        }
        if (json_object_object_get_ex(mqtt_obj, "client_id", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.client_id,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.client_id));
        }
        if (json_object_object_get_ex(mqtt_obj, "username", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.username,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.username));
        }
        if (json_object_object_get_ex(mqtt_obj, "password", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.password,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.password));
        }
        if (json_object_object_get_ex(mqtt_obj, "topic_telemetry", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.topic_telemetry,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.topic_telemetry));
        }
        if (json_object_object_get_ex(mqtt_obj, "topic_attributes", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.topic_attributes,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.topic_attributes));
        }
        if (json_object_object_get_ex(mqtt_obj, "topic_control", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.topic_control,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.topic_control));
        }
        if (json_object_object_get_ex(mqtt_obj, "topic_status", &temp_obj))
        {
            safe_strncpy(node_registry.mqtt_config.topic_status,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.mqtt_config.topic_status));
        }

        // Integer fields
        if (json_object_object_get_ex(mqtt_obj, "broker_port", &temp_obj))
            node_registry.mqtt_config.broker_port = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "qos", &temp_obj))
            node_registry.mqtt_config.qos = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "publish_interval", &temp_obj))
            node_registry.mqtt_config.publish_interval = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "connection_timeout", &temp_obj))
            node_registry.mqtt_config.connection_timeout = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "reconnect_delay_ms", &temp_obj))
            node_registry.mqtt_config.reconnect_delay_ms = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "loop_interval_ms", &temp_obj))
            node_registry.mqtt_config.loop_interval_ms = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "payload_buffer_size", &temp_obj))
            node_registry.mqtt_config.payload_buffer_size = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(mqtt_obj, "attributes_buffer_size", &temp_obj))
            node_registry.mqtt_config.attributes_buffer_size = json_object_get_int(temp_obj);

        // Parse system fields
        json_object *system_fields_obj;
        if (json_object_object_get_ex(mqtt_obj, "system_fields", &system_fields_obj))
        {
            if (json_object_object_get_ex(system_fields_obj, "data_source", &temp_obj))
            {
                safe_strncpy(node_registry.mqtt_config.system_fields.data_source,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.mqtt_config.system_fields.data_source));
            }
            if (json_object_object_get_ex(system_fields_obj, "include_timestamp", &temp_obj))
                node_registry.mqtt_config.system_fields.include_timestamp = json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(system_fields_obj, "include_gateway_ip", &temp_obj))
                node_registry.mqtt_config.system_fields.include_gateway_ip = json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(system_fields_obj, "include_node_count", &temp_obj))
                node_registry.mqtt_config.system_fields.include_node_count = json_object_get_boolean(temp_obj);
        }
    }
    // Parse TCP config:
    json_object *tcp_obj;
    if (json_object_object_get_ex(effective_root, "tcp_config", &tcp_obj))
    {
        json_object *temp_obj;

        // Parse basic TCP fields
        if (json_object_object_get_ex(tcp_obj, "server_host", &temp_obj))
        {
            safe_strncpy(node_registry.tcp_config.server_host,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.tcp_config.server_host));
        }
        if (json_object_object_get_ex(tcp_obj, "server_port", &temp_obj))
            node_registry.tcp_config.server_port = (uint16_t)json_object_get_int(temp_obj);

        if (json_object_object_get_ex(tcp_obj, "config_server_host", &temp_obj))
        {
            safe_strncpy(node_registry.tcp_config.config_server_host,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.tcp_config.config_server_host));
        }
        if (json_object_object_get_ex(tcp_obj, "config_server_port", &temp_obj))
            node_registry.tcp_config.config_server_port = (uint16_t)json_object_get_int(temp_obj);

        if (json_object_object_get_ex(tcp_obj, "client_id", &temp_obj))
        {
            safe_strncpy(node_registry.tcp_config.client_id,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.tcp_config.client_id));
        }
        if (json_object_object_get_ex(tcp_obj, "protocol_version", &temp_obj))
        {
            safe_strncpy(node_registry.tcp_config.protocol_version,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.tcp_config.protocol_version));
        }
        if (json_object_object_get_ex(tcp_obj, "data_format", &temp_obj))
        {
            safe_strncpy(node_registry.tcp_config.data_format,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.tcp_config.data_format));
        }

        // Parse timing and buffer fields
        if (json_object_object_get_ex(tcp_obj, "send_interval", &temp_obj))
            node_registry.tcp_config.send_interval = (uint16_t)json_object_get_int(temp_obj);
        if (json_object_object_get_ex(tcp_obj, "connection_timeout", &temp_obj))
            node_registry.tcp_config.connection_timeout = (uint16_t)json_object_get_int(temp_obj);
        if (json_object_object_get_ex(tcp_obj, "reconnect_delay_ms", &temp_obj))
            node_registry.tcp_config.reconnect_delay_ms = (uint16_t)json_object_get_int(temp_obj);
        if (json_object_object_get_ex(tcp_obj, "loop_interval_ms", &temp_obj))
            node_registry.tcp_config.loop_interval_ms = (uint16_t)json_object_get_int(temp_obj);
        if (json_object_object_get_ex(tcp_obj, "payload_buffer_size", &temp_obj))
            node_registry.tcp_config.payload_buffer_size = (uint16_t)json_object_get_int(temp_obj);
        if (json_object_object_get_ex(tcp_obj, "receive_buffer_size", &temp_obj))
            node_registry.tcp_config.receive_buffer_size = (uint16_t)json_object_get_int(temp_obj);

        // Parse nested socket_options object
        json_object *socket_options_obj;
        if (json_object_object_get_ex(tcp_obj, "socket_options", &socket_options_obj))
        {
            if (json_object_object_get_ex(socket_options_obj, "keepalive", &temp_obj))
                node_registry.tcp_config.socket_options.keepalive = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(socket_options_obj, "keepalive_idle", &temp_obj))
                node_registry.tcp_config.socket_options.keepalive_idle = (uint16_t)json_object_get_int(temp_obj);
            if (json_object_object_get_ex(socket_options_obj, "keepalive_interval", &temp_obj))
                node_registry.tcp_config.socket_options.keepalive_interval = (uint16_t)json_object_get_int(temp_obj);
            if (json_object_object_get_ex(socket_options_obj, "keepalive_count", &temp_obj))
                node_registry.tcp_config.socket_options.keepalive_count = (uint8_t)json_object_get_int(temp_obj);
            if (json_object_object_get_ex(socket_options_obj, "tcp_nodelay", &temp_obj))
                node_registry.tcp_config.socket_options.tcp_nodelay = (uint8_t)json_object_get_boolean(temp_obj);
        }

        // Parse nested system_fields object
        json_object *tcp_system_fields_obj;
        if (json_object_object_get_ex(tcp_obj, "system_fields", &tcp_system_fields_obj))
        {
            if (json_object_object_get_ex(tcp_system_fields_obj, "data_source", &temp_obj))
            {
                safe_strncpy(node_registry.tcp_config.system_fields.data_source,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.tcp_config.system_fields.data_source));
            }
            if (json_object_object_get_ex(tcp_system_fields_obj, "include_timestamp", &temp_obj))
                node_registry.tcp_config.system_fields.include_timestamp = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(tcp_system_fields_obj, "include_gateway_ip", &temp_obj))
                node_registry.tcp_config.system_fields.include_gateway_ip = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(tcp_system_fields_obj, "include_node_count", &temp_obj))
                node_registry.tcp_config.system_fields.include_node_count = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(tcp_system_fields_obj, "include_system_info", &temp_obj))
                node_registry.tcp_config.system_fields.include_system_info = (uint8_t)json_object_get_boolean(temp_obj);
        }

        // Parse nested protocol_settings object
        json_object *protocol_settings_obj;
        if (json_object_object_get_ex(tcp_obj, "protocol_settings", &protocol_settings_obj))
        {
            if (json_object_object_get_ex(protocol_settings_obj, "message_delimiter", &temp_obj))
            {
                safe_strncpy(node_registry.tcp_config.protocol_settings.message_delimiter,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.tcp_config.protocol_settings.message_delimiter));
            }
            if (json_object_object_get_ex(protocol_settings_obj, "max_message_size", &temp_obj))
                node_registry.tcp_config.protocol_settings.max_message_size = (uint16_t)json_object_get_int(temp_obj);
            if (json_object_object_get_ex(protocol_settings_obj, "compression_enabled", &temp_obj))
                node_registry.tcp_config.protocol_settings.compression_enabled = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(protocol_settings_obj, "encryption_enabled", &temp_obj))
                node_registry.tcp_config.protocol_settings.encryption_enabled = (uint8_t)json_object_get_boolean(temp_obj);
        }

        // Parse nested features object
        json_object *features_obj;
        if (json_object_object_get_ex(tcp_obj, "features", &features_obj))
        {
            if (json_object_object_get_ex(features_obj, "config_download", &temp_obj))
                node_registry.tcp_config.features.config_download = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(features_obj, "control_commands", &temp_obj))
                node_registry.tcp_config.features.control_commands = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(features_obj, "telemetry_upload", &temp_obj))
                node_registry.tcp_config.features.telemetry_upload = (uint8_t)json_object_get_boolean(temp_obj);
            if (json_object_object_get_ex(features_obj, "status_reporting", &temp_obj))
                node_registry.tcp_config.features.status_reporting = (uint8_t)json_object_get_boolean(temp_obj);
        }

        // Parse UDP config
        json_object *udp_obj;
        if (json_object_object_get_ex(effective_root, "udp_config", &udp_obj))
        {
            json_object *temp_obj;

            // Parse basic UDP fields
            if (json_object_object_get_ex(udp_obj, "server_host", &temp_obj))
            {
                safe_strncpy(node_registry.udp_config.server_host,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.udp_config.server_host));
            }

            if (json_object_object_get_ex(udp_obj, "server_port", &temp_obj))
                node_registry.udp_config.server_port = (uint16_t)json_object_get_int(temp_obj);

            if (json_object_object_get_ex(udp_obj, "client_id", &temp_obj))
            {
                safe_strncpy(node_registry.udp_config.client_id,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.udp_config.client_id));
            }

            if (json_object_object_get_ex(udp_obj, "protocol_version", &temp_obj))
            {
                safe_strncpy(node_registry.udp_config.protocol_version,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.udp_config.protocol_version));
            }

            if (json_object_object_get_ex(udp_obj, "data_format", &temp_obj))
            {
                safe_strncpy(node_registry.udp_config.data_format,
                             json_object_get_string(temp_obj),
                             sizeof(node_registry.udp_config.data_format));
            }

            // Parse timing and buffer fields
            if (json_object_object_get_ex(udp_obj, "send_interval", &temp_obj))
                node_registry.udp_config.send_interval = (uint16_t)json_object_get_int(temp_obj);

            if (json_object_object_get_ex(udp_obj, "connection_timeout", &temp_obj))
                node_registry.udp_config.connection_timeout = (uint16_t)json_object_get_int(temp_obj);

            if (json_object_object_get_ex(udp_obj, "reconnect_delay_ms", &temp_obj))
                node_registry.udp_config.reconnect_delay_ms = (uint16_t)json_object_get_int(temp_obj);

            if (json_object_object_get_ex(udp_obj, "loop_interval_ms", &temp_obj))
                node_registry.udp_config.loop_interval_ms = (uint16_t)json_object_get_int(temp_obj);

            if (json_object_object_get_ex(udp_obj, "payload_buffer_size", &temp_obj))
                node_registry.udp_config.payload_buffer_size = (uint16_t)json_object_get_int(temp_obj);

            if (json_object_object_get_ex(udp_obj, "receive_buffer_size", &temp_obj))
                node_registry.udp_config.receive_buffer_size = (uint16_t)json_object_get_int(temp_obj);

            // Parse nested socket_options object
            json_object *socket_options_obj;
            if (json_object_object_get_ex(udp_obj, "socket_options", &socket_options_obj))
            {
                if (json_object_object_get_ex(socket_options_obj, "broadcast", &temp_obj))
                    node_registry.udp_config.socket_options.broadcast = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(socket_options_obj, "reuse_addr", &temp_obj))
                    node_registry.udp_config.socket_options.reuse_addr = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(socket_options_obj, "reuse_port", &temp_obj))
                    node_registry.udp_config.socket_options.reuse_port = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(socket_options_obj, "receive_timeout_ms", &temp_obj))
                    node_registry.udp_config.socket_options.receive_timeout_ms = (uint16_t)json_object_get_int(temp_obj);

                if (json_object_object_get_ex(socket_options_obj, "send_timeout_ms", &temp_obj))
                    node_registry.udp_config.socket_options.send_timeout_ms = (uint16_t)json_object_get_int(temp_obj);
            }

            // Parse nested system_fields object
            json_object *udp_system_fields_obj;
            if (json_object_object_get_ex(udp_obj, "system_fields", &udp_system_fields_obj))
            {
                if (json_object_object_get_ex(udp_system_fields_obj, "data_source", &temp_obj))
                {
                    safe_strncpy(node_registry.udp_config.system_fields.data_source,
                                 json_object_get_string(temp_obj),
                                 sizeof(node_registry.udp_config.system_fields.data_source));
                }

                if (json_object_object_get_ex(udp_system_fields_obj, "include_timestamp", &temp_obj))
                    node_registry.udp_config.system_fields.include_timestamp = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_system_fields_obj, "include_gateway_ip", &temp_obj))
                    node_registry.udp_config.system_fields.include_gateway_ip = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_system_fields_obj, "include_node_count", &temp_obj))
                    node_registry.udp_config.system_fields.include_node_count = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_system_fields_obj, "include_system_info", &temp_obj))
                    node_registry.udp_config.system_fields.include_system_info = (uint8_t)json_object_get_boolean(temp_obj);
            }

            // Parse nested protocol_settings object
            json_object *udp_protocol_settings_obj;
            if (json_object_object_get_ex(udp_obj, "protocol_settings", &udp_protocol_settings_obj))
            {
                if (json_object_object_get_ex(udp_protocol_settings_obj, "message_delimiter", &temp_obj))
                {
                    safe_strncpy(node_registry.udp_config.protocol_settings.message_delimiter,
                                 json_object_get_string(temp_obj),
                                 sizeof(node_registry.udp_config.protocol_settings.message_delimiter));
                }

                if (json_object_object_get_ex(udp_protocol_settings_obj, "max_message_size", &temp_obj))
                    node_registry.udp_config.protocol_settings.max_message_size = (uint16_t)json_object_get_int(temp_obj);

                if (json_object_object_get_ex(udp_protocol_settings_obj, "compression_enabled", &temp_obj))
                    node_registry.udp_config.protocol_settings.compression_enabled = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_protocol_settings_obj, "encryption_enabled", &temp_obj))
                    node_registry.udp_config.protocol_settings.encryption_enabled = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_protocol_settings_obj, "checksum_enabled", &temp_obj))
                    node_registry.udp_config.protocol_settings.checksum_enabled = (uint8_t)json_object_get_boolean(temp_obj);
            }

            // Parse nested features object
            json_object *udp_features_obj;
            if (json_object_object_get_ex(udp_obj, "features", &udp_features_obj))
            {
                if (json_object_object_get_ex(udp_features_obj, "control_commands", &temp_obj))
                    node_registry.udp_config.features.control_commands = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_features_obj, "telemetry_upload", &temp_obj))
                    node_registry.udp_config.features.telemetry_upload = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_features_obj, "status_reporting", &temp_obj))
                    node_registry.udp_config.features.status_reporting = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_features_obj, "broadcast_discovery", &temp_obj))
                    node_registry.udp_config.features.broadcast_discovery = (uint8_t)json_object_get_boolean(temp_obj);

                if (json_object_object_get_ex(udp_features_obj, "multicast_support", &temp_obj))
                    node_registry.udp_config.features.multicast_support = (uint8_t)json_object_get_boolean(temp_obj);
            }
            // Parse nodes array
            json_object *nodes_array;
            if (json_object_object_get_ex(effective_root, "nodes", &nodes_array) ||
                json_object_object_get_ex(effective_root, "node", &nodes_array))
            {
                uint32_t array_len = json_object_array_length(nodes_array);
                if (array_len > 0)
                {
                    node_registry.nodes = malloc(sizeof(node_config_t) * array_len);
                    if (!node_registry.nodes)
                    {
#ifdef DEBUG
                        printf("Error: Failed to allocate memory for nodes\n");
#endif
                        json_object_put(root);
                        return -1;
                    }
                    node_registry.capacity = array_len;
                    node_registry.count = 0;
                    for (uint32_t i = 0; i < array_len; i++)
                    {
                        json_object *node_obj = json_object_array_get_idx(nodes_array, i);
                        if (parse_node_config(node_obj, &node_registry.nodes[i]) == 0)
                        {
                            node_registry.count++;
                        }
                        else
                        {
#ifdef DEBUG
                            printf("Warning: Failed to parse node at index %d\n", i);
#endif
                        }
                    }
                }
            }
            json_object_put(root);
#ifdef DEBUG
            printf("Configuration loaded: %d nodes\n", node_registry.count);
            // DEBUG: Validate broker_host after parsing
            printf("DEBUG: Broker host after parsing: '%s' (len=%zu)\n",
                   node_registry.mqtt_config.broker_host,
                   strlen(node_registry.mqtt_config.broker_host));
            printf("MQTT broker: %s:%d\n", node_registry.mqtt_config.broker_host,
                   node_registry.mqtt_config.broker_port);
#endif
            return 0;
        }
    }
    // Standard getter functions
    node_config_t *get_node_by_id(uint32_t node_id)
    {
        for (uint32_t i = 0; i < node_registry.count; i++)
        {
            if (node_registry.nodes[i].node_id == node_id)
            {
                return &node_registry.nodes[i];
            }
        }
        return NULL;
    }

    node_config_t *get_node_by_index(uint32_t index)
    {
        if (index >= 0 && index < node_registry.count)
        {
            return &node_registry.nodes[index];
        }
        return NULL;
    }

    uint32_t get_node_count(void)
    {
        return node_registry.count;
    }

    menu_item_t *get_menu_item_by_cmd(node_config_t * node, uint32_t cmd)
    {
        if (!node)
            return NULL;
        for (uint32_t i = 0; i < node->menu_count; i++)
        {
            if (node->menu_items[i].cmd == cmd)
            {
                return &node->menu_items[i];
            }
        }
        return NULL;
    }

    // System config getters
    uint32_t get_ui_refresh_delay(void) { return node_registry.ui_refresh_delay; }
    uint32_t get_default_baudrate(void) { return node_registry.default_baudrate; }
    const char *get_default_device(void) { return node_registry.default_device; }
    uint32_t get_startup_clear_duration(void) { return node_registry.startup_clear_duration; }

    // Detection getters
    system_info_t *get_system_info(void) { return &node_registry.system_info; }
    uart_config_t *get_uart_config(void) { return &node_registry.uart_config; }
    modbus_config_t *get_modbus_config(void) { return &node_registry.modbus_config; }
    can_config_t *get_can_config(void) { return &node_registry.can_config; }
    mqtt_config_t *get_mqtt_config(void) { return &node_registry.mqtt_config; }
    tcp_config_t *get_tcp_config(void) { return &node_registry.tcp_config; }
    udp_config_t *get_udp_config(void) { return &node_registry.udp_config; }

    // Control queue functions
    int add_control_command(uint32_t node_id, uint32_t cmd_id, const char *params)
    {
        pthread_mutex_lock(&node_registry.control_queue.mutex);
        if (node_registry.control_queue.count >= 100)
        {
#ifdef DEBUG
            printf("Warning: Control queue full\n");
#endif
            pthread_mutex_unlock(&node_registry.control_queue.mutex);
            return -1;
        }
        server_control_cmd_t *cmd = &node_registry.control_queue.commands[node_registry.control_queue.tail];
        cmd->node_id = node_id;
        cmd->cmd_id = cmd_id;
        if (params)
        {
            safe_strncpy(cmd->params, params, sizeof(cmd->params));
        }
        else
        {
            cmd->params[0] = '\0';
        }
        cmd->timestamp = time(NULL);
        cmd->processed = 0;
        node_registry.control_queue.tail = (node_registry.control_queue.tail + 1) % 100;
        node_registry.control_queue.count++;
        pthread_cond_signal(&node_registry.control_queue.cond);
        pthread_mutex_unlock(&node_registry.control_queue.mutex);
        return 0;
    }

    int get_control_command(server_control_cmd_t * cmd)
    {
        if (!cmd)
            return -1;
        pthread_mutex_lock(&node_registry.control_queue.mutex);
        if (node_registry.control_queue.count == 0)
        {
            pthread_mutex_unlock(&node_registry.control_queue.mutex);
            return -1;
        }
        *cmd = node_registry.control_queue.commands[node_registry.control_queue.head];
        node_registry.control_queue.head = (node_registry.control_queue.head + 1) % 100;
        node_registry.control_queue.count--;
        pthread_mutex_unlock(&node_registry.control_queue.mutex);
        return 0;
    }

    communication_type_t get_communication_type(void)
    {
        return node_registry.communication_type;
    }

    void set_communication_type(communication_type_t type)
    {
        if (type >= 0 && type < COMM_TYPE_COUNT)
        {
            node_registry.communication_type = type;
        }
    }

    const char *get_communication_type_name(communication_type_t type)
    {
        switch (type)
        {
        case COMM_TYPE_MQTT:
            return "MQTT";
        case COMM_TYPE_HTTP:
            return "HTTP";
        case COMM_TYPE_WEBSOCKET:
            return "WebSocket";
        case COMM_TYPE_TCP:
            return "TCP";
        default:
            return "Unknown";
        }
    }

    /**
     * Cleanup all resources
     */
    void cleanup_nodes_config(void)
    {
        for (uint32_t i = 0; i < node_registry.count; i++)
        {
            node_config_t *node = &node_registry.nodes[i];

            if (node->menu_items)
            {
                free(node->menu_items);
            }

            if (node->detection_commands)
            {
                free(node->detection_commands);
            }

            if (node->mqtt_data)
            {
                free_shared_data(node->mqtt_data);
            }

            if (node->tcp_data)
            {
                free_shared_data(node->tcp_data);
            }

            if (node->udp_data)
            { // ADD THIS BLOCK
                free_shared_data(node->udp_data);
            }
        }

        if (node_registry.nodes)
        {
            free(node_registry.nodes);
        }

        if (node_registry.uart_config.supported_baudrates)
        {
            free(node_registry.uart_config.supported_baudrates);
        }

        pthread_mutex_destroy(&node_registry.control_queue.mutex);
        pthread_cond_destroy(&node_registry.control_queue.cond);
        memset(&node_registry, 0, sizeof(node_registry));

#ifdef DEBUG
        printf("Configuration cleanup completed\n");
#endif
    }

    // Thread-safe config reload
    int safe_reload_config(void)
    {
#ifdef DEBUG
        printf("Starting safe config reload\n");
#endif

        // Signal all threads that config is reloading
        pthread_mutex_lock(&config_mutex);
        config_reloading = 1;

        // Give other threads time to finish current operations
        pthread_mutex_unlock(&config_mutex);
        usleep(200 * 1000); // 200ms

        // Now acquire lock for full reload
        pthread_mutex_lock(&config_mutex);

#ifdef DEBUG
        printf("Cleaning up old config\n");
#endif
        cleanup_nodes_config();

#ifdef DEBUG
        printf("Loading new config\n");
#endif
        uint32_t load_result = 0;
        char config_path[64];
        snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, CONFIG_FILE);
        char backup_path[64];
        snprintf(backup_path, sizeof(backup_path), "%s/%s", CONFIG_DIR, FALLBACK_CONFIG_FILE);
        if (load_nodes_config(config_path) == 0)
        {
            load_result = 1;
        }
        else if (load_nodes_config(backup_path) == 0)
        {
            load_result = 1;
        }

        config_reloading = 0;
        pthread_mutex_unlock(&config_mutex);

#ifdef DEBUG
        printf("Config reload completed: %s\n", load_result ? "SUCCESS" : "FAILED");
#endif

        return load_result ? 0 : -1;
    }

    // Safe node access functions
    node_config_t *safe_get_node_by_index(uint32_t index)
    {
        pthread_mutex_lock(&config_mutex);
        if (config_reloading)
        {
            pthread_mutex_unlock(&config_mutex);
            return NULL;
        }
        node_config_t *node = get_node_by_index(index);
        pthread_mutex_unlock(&config_mutex);
        return node;
    }

    uint32_t safe_get_node_count(void)
    {
        pthread_mutex_lock(&config_mutex);
        if (config_reloading)
        {
            pthread_mutex_unlock(&config_mutex);
            return 0;
        }
        uint32_t count = get_node_count();
        pthread_mutex_unlock(&config_mutex);
        return count;
    }
