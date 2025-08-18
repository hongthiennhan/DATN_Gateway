#include "main.h"
#include "node_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

// Global node registry
static node_registry_t node_registry = {0};
// Global config protection
pthread_mutex_t config_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int config_reloading = 0;

// Shared data structure for thread communication
typedef struct shared_data_s
{
    void *data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

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
 * Convert hex string to integer
 */
uint32_t hex_string_to_int(const char *hex_str)
{
    if (!hex_str)
        return 0;
    if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0)
    {
        hex_str += 2;
    }
    return (uint32_t)strtoul(hex_str, NULL, 16);
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
static int parse_detection_commands(json_object *detection_array, node_config_t *node)
{
    if (!detection_array || !node)
        return -1;
    int array_len = json_object_array_length(detection_array);
    if (array_len <= 0)
        return 0;
    node->detection_commands = malloc(sizeof(detection_cmd_t) * array_len);
    if (!node->detection_commands)
        return -1;
    node->detection_count = array_len;
    for (int i = 0; i < array_len; i++)
    {
        json_object *cmd_obj = json_object_array_get_idx(detection_array, i);
        detection_cmd_t *cmd = &node->detection_commands[i];
        json_object *temp_obj;
        memset(cmd, 0, sizeof(detection_cmd_t));
        if (json_object_object_get_ex(cmd_obj, "command", &temp_obj))
        {
            cmd->command = (uint8_t)json_object_get_int(temp_obj);
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
static int parse_menu_items(json_object *menu_array, node_config_t *node)
{
    if (!menu_array || !node)
        return -1;
    int array_len = json_object_array_length(menu_array);
    if (array_len <= 0)
        return 0;
    node->menu_items = malloc(sizeof(menu_item_t) * array_len);
    if (!node->menu_items)
        return -1;
    node->menu_count = array_len;
    for (int i = 0; i < array_len; i++)
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
        if (json_object_object_get_ex(item_obj, "uart_cmd", &temp_obj))
        {
            safe_strncpy(item->uart_cmd, json_object_get_string(temp_obj), sizeof(item->uart_cmd));
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
static int parse_node_config(json_object *node_obj, node_config_t *node)
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
    if (json_object_object_get_ex(node_obj, "type", &temp_obj))
    {
        safe_strncpy(node->type, json_object_get_string(temp_obj), sizeof(node->type));
    }
    if (json_object_object_get_ex(node_obj, "data_format", &temp_obj))
    {
        safe_strncpy(node->data_format, json_object_get_string(temp_obj), sizeof(node->data_format));
    }
    if (json_object_object_get_ex(node_obj, "reflash_script", &temp_obj))
    {
        safe_strncpy(node->reflash_script, json_object_get_string(temp_obj), sizeof(node->reflash_script));
    }
    if (json_object_object_get_ex(node_obj, "is_actuator", &temp_obj))
    {
        node->is_actuator = json_object_get_boolean(temp_obj);
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
    node_registry.config_mode = 0;

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
        if (json_object_object_get_ex(system_obj, "auto_read_command_id", &temp_obj))
            node_registry.auto_read_command_id = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "uart_clear_timeout", &temp_obj))
            node_registry.uart_clear_timeout = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "ui_refresh_delay", &temp_obj))
            node_registry.ui_refresh_delay = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "uart_wait_timeout", &temp_obj))
            node_registry.uart_wait_timeout = json_object_get_int(temp_obj);
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
        if (json_object_object_get_ex(uart_obj, "config_file_path", &temp_obj))
        {
            safe_strncpy(node_registry.uart_config.config_file_path,
                         json_object_get_string(temp_obj),
                         sizeof(node_registry.uart_config.config_file_path));
        }

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
            if (json_object_object_get_ex(timing_obj, "select_timeout_ms", &temp_obj))
                node_registry.uart_config.select_timeout_ms = json_object_get_int(temp_obj);
        }
        if (json_object_object_get_ex(uart_obj, "default_baudrate_fallback", &temp_obj))
            node_registry.uart_config.default_baudrate_fallback = json_object_get_int(temp_obj);
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

    // Parse nodes array
    json_object *nodes_array;
    if (json_object_object_get_ex(effective_root, "nodes", &nodes_array) ||
        json_object_object_get_ex(effective_root, "node", &nodes_array))
    {
        int array_len = json_object_array_length(nodes_array);
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
            for (int i = 0; i < array_len; i++)
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

// Standard getter functions
node_config_t *get_node_by_id(int node_id)
{
    for (int i = 0; i < node_registry.count; i++)
    {
        if (node_registry.nodes[i].node_id == node_id)
        {
            return &node_registry.nodes[i];
        }
    }
    return NULL;
}

node_config_t *get_node_by_index(int index)
{
    if (index >= 0 && index < node_registry.count)
    {
        return &node_registry.nodes[index];
    }
    return NULL;
}

int get_node_count(void)
{
    return node_registry.count;
}

menu_item_t *get_menu_item_by_cmd(node_config_t *node, int cmd)
{
    if (!node)
        return NULL;
    for (int i = 0; i < node->menu_count; i++)
    {
        if (node->menu_items[i].cmd == cmd)
        {
            return &node->menu_items[i];
        }
    }
    return NULL;
}

// System config getters
int get_auto_read_command_id(void) { return node_registry.auto_read_command_id; }
int get_uart_clear_timeout(void) { return node_registry.uart_clear_timeout; }
int get_ui_refresh_delay(void) { return node_registry.ui_refresh_delay; }
int get_uart_wait_timeout(void) { return node_registry.uart_wait_timeout; }
int get_default_baudrate(void) { return node_registry.default_baudrate; }
const char *get_default_device(void) { return node_registry.default_device; }
int get_startup_clear_duration(void) { return node_registry.startup_clear_duration; }

// Detection getters
system_info_t *get_system_info(void) { return &node_registry.system_info; }
uart_config_t *get_uart_config(void) { return &node_registry.uart_config; }
const char *get_uart_config_file_path(void) { return node_registry.uart_config.config_file_path; }
mqtt_config_t *get_mqtt_config(void) { return &node_registry.mqtt_config; }

// Control queue functions
int add_control_command(int node_id, int cmd_id, const char *params)
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

int get_control_command(server_control_cmd_t *cmd)
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

control_queue_t *get_control_queue(void)
{
    return &node_registry.control_queue;
}

void set_config_mode(int enabled)
{
    node_registry.config_mode = enabled;
}

int get_config_mode(void)
{
    return node_registry.config_mode;
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
    for (int i = 0; i < node_registry.count; i++)
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
int safe_reload_config(void) {
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
    int load_result = 0;
    if (load_nodes_config("../config.json") == 0) {
        load_result = 1;
    } else if (load_nodes_config("../nodes_config.json") == 0) {
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
node_config_t *safe_get_node_by_index(int index) {
    pthread_mutex_lock(&config_mutex);
    if (config_reloading) {
        pthread_mutex_unlock(&config_mutex);
        return NULL;
    }
    node_config_t *node = get_node_by_index(index);
    pthread_mutex_unlock(&config_mutex);
    return node;
}

int safe_get_node_count(void) {
    pthread_mutex_lock(&config_mutex);
    if (config_reloading) {
        pthread_mutex_unlock(&config_mutex);
        return 0;
    }
    int count = get_node_count();
    pthread_mutex_unlock(&config_mutex);
    return count;
}
