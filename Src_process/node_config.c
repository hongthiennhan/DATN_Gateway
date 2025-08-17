#include "main.h"
#include "node_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// Global node registry
static node_registry_t node_registry = {0};

// Shared data structure for thread communication
typedef struct shared_data_s {
    void *data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

// Safe string copy with guaranteed null termination
static void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0)
        return;
    size_t len = strlen(src);
    if (len >= dest_size)
        len = dest_size - 1;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// Convert hex string to integer
uint32_t hex_string_to_int(const char *hex_str) {
    if (!hex_str)
        return 0;
    if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0)
        hex_str += 2;
    return (uint32_t)strtoul(hex_str, NULL, 16);
}

// Allocate shared data structure
static shared_data_t *allocate_shared_data(void) {
    shared_data_t *shared = malloc(sizeof(shared_data_t));
    if (!shared)
        return NULL;
    shared->data = NULL;
    if (pthread_mutex_init(&shared->mutex, NULL) != 0) {
        free(shared);
        return NULL;
    }
    if (pthread_cond_init(&shared->cond, NULL) != 0) {
        pthread_mutex_destroy(&shared->mutex);
        free(shared);
        return NULL;
    }
    return shared;
}

// Free shared data structure safely
static void free_shared_data(shared_data_t *shared) {
    if (!shared)
        return;
    pthread_mutex_destroy(&shared->mutex);
    pthread_cond_destroy(&shared->cond);
    if (shared->data)
        free(shared->data);
    free(shared);
}

// Parse detection commands from JSON with safety checks
static int parse_detection_commands(json_object *detection_array, node_config_t *node) {
    if (!detection_array || !node)
        return -1;
    int array_len = json_object_array_length(detection_array);
    if (array_len <= 0) {
        node->detection_commands = NULL;
        node->detection_count = 0;
        return 0;
    }
    node->detection_commands = malloc(sizeof(detection_cmd_t) * array_len);
    if (!node->detection_commands)
        return -1;
    node->detection_count = array_len;
    for (int i = 0; i < array_len; i++) {
        json_object *cmd_obj = json_object_array_get_idx(detection_array, i);
        detection_cmd_t *cmd = &node->detection_commands[i];
        json_object *temp_obj;
        memset(cmd, 0, sizeof(detection_cmd_t));
        if (json_object_object_get_ex(cmd_obj, "command", &temp_obj))
            cmd->command = (uint8_t)json_object_get_int(temp_obj);
        if (json_object_object_get_ex(cmd_obj, "expected_response", &temp_obj))
            safe_strncpy(cmd->expected_response, json_object_get_string(temp_obj), sizeof(cmd->expected_response));
        if (json_object_object_get_ex(cmd_obj, "timeout_ms", &temp_obj))
            cmd->timeout_ms = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(cmd_obj, "description", &temp_obj))
            safe_strncpy(cmd->description, json_object_get_string(temp_obj), sizeof(cmd->description));
    }
    return 0;
}

// Parse menu items for node with safety checks
static int parse_menu_items(json_object *menu_array, node_config_t *node) {
    if (!menu_array || !node)
        return -1;
    int array_len = json_object_array_length(menu_array);
    if (array_len <= 0) {
        node->menu_items = NULL;
        node->menu_count = 0;
        return 0;
    }
    node->menu_items = malloc(sizeof(menu_item_t) * array_len);
    if (!node->menu_items)
        return -1;
    node->menu_count = array_len;
    for (int i = 0; i < array_len; i++) {
        json_object *item_obj = json_object_array_get_idx(menu_array, i);
        menu_item_t *item = &node->menu_items[i];
        json_object *temp_obj;
        memset(item, 0, sizeof(menu_item_t));
        if (json_object_object_get_ex(item_obj, "cmd", &temp_obj))
            item->cmd = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(item_obj, "label", &temp_obj))
            safe_strncpy(item->label, json_object_get_string(temp_obj), sizeof(item->label));
        if (json_object_object_get_ex(item_obj, "uart_cmd", &temp_obj))
            safe_strncpy(item->uart_cmd, json_object_get_string(temp_obj), sizeof(item->uart_cmd));
        if (json_object_object_get_ex(item_obj, "hex_value", &temp_obj))
            safe_strncpy(item->hex_value, json_object_get_string(temp_obj), sizeof(item->hex_value));
        if (json_object_object_get_ex(item_obj, "timeout_ms", &temp_obj))
            item->timeout_ms = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(item_obj, "is_direct", &temp_obj))
            item->is_direct = json_object_get_boolean(temp_obj);
    }
    return 0;
}

// Parse individual node configuration with fallback handling
static int parse_node_config(json_object *node_obj, node_config_t *node) {
    if (!node_obj || !node)
        return -1;
    json_object *temp_obj;
    memset(node, 0, sizeof(node_config_t));
    
    // Parse basic node info
    if (json_object_object_get_ex(node_obj, "id", &temp_obj))
        node->node_id = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(node_obj, "name", &temp_obj))
        safe_strncpy(node->name, json_object_get_string(temp_obj), sizeof(node->name));
    if (json_object_object_get_ex(node_obj, "type", &temp_obj))
        safe_strncpy(node->type, json_object_get_string(temp_obj), sizeof(node->type));
    if (json_object_object_get_ex(node_obj, "data_format", &temp_obj))
        safe_strncpy(node->data_format, json_object_get_string(temp_obj), sizeof(node->data_format));
    if (json_object_object_get_ex(node_obj, "reflash_script", &temp_obj))
        safe_strncpy(node->reflash_script, json_object_get_string(temp_obj), sizeof(node->reflash_script));
    if (json_object_object_get_ex(node_obj, "is_actuator", &temp_obj))
        node->is_actuator = json_object_get_boolean(temp_obj);
    
    // Parse detection commands with fallback
    json_object *detection_array;
    if (json_object_object_get_ex(node_obj, "detection_commands", &detection_array)) {
        parse_detection_commands(detection_array, node);
    } else {
        node->detection_commands = NULL;
        node->detection_count = 0;
        #ifdef DEBUG
        printf("Node %d: No detection_commands found, using fallback\n", node->node_id);
        #endif
    }
    
    // Parse menu items with fallback
    json_object *menu_array;
    if (json_object_object_get_ex(node_obj, "menu_items", &menu_array)) {
        parse_menu_items(menu_array, node);
    } else {
        node->menu_items = NULL;
        node->menu_count = 0;
        #ifdef DEBUG
        printf("Node %d: No menu_items found, using fallback\n", node->node_id);
        #endif
    }
    
    // Initialize runtime data
    node->detected = 0;
    node->last_detection = 0;
    node->last_data_received = 0;
    node->mqtt_data = allocate_shared_data();
    return 0;
}

// Load complete configuration from JSON file
int load_nodes_config(const char *config_file) {
    #ifdef DEBUG
    printf("CHECKPOINT: Loading configuration from: %s\n", config_file);
    #endif
    
    json_object *root = json_object_from_file(config_file);
    if (!root) {
        #ifdef DEBUG
        printf("ERROR: Cannot load config file %s\n", config_file);
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
        json_object_object_get_ex(shared_obj, "config", &config_obj)) {
        effective_root = config_obj;
        #ifdef DEBUG
        printf("CHECKPOINT: Using ThingsBoard wrapper format\n");
        #endif
    }
    
    // Parse system configuration with defaults
    json_object *system_obj;
    if (json_object_object_get_ex(effective_root, "system_config", &system_obj)) {
        json_object *temp_obj;
        if (json_object_object_get_ex(system_obj, "auto_read_command_id", &temp_obj))
            node_registry.auto_read_command_id = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "uart_clear_timeout", &temp_obj))
            node_registry.uart_clear_timeout = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "ui_refresh_delay", &temp_obj))
            node_registry.ui_refresh_delay = json_object_get_int(temp_obj);
        else
            node_registry.ui_refresh_delay = 1000; // Default 1 second
        if (json_object_object_get_ex(system_obj, "uart_wait_timeout", &temp_obj))
            node_registry.uart_wait_timeout = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "default_baudrate", &temp_obj))
            node_registry.default_baudrate = json_object_get_int(temp_obj);
        else
            node_registry.default_baudrate = 115200; // Default
        if (json_object_object_get_ex(system_obj, "default_device", &temp_obj))
            safe_strncpy(node_registry.default_device, json_object_get_string(temp_obj), sizeof(node_registry.default_device));
        else
            strcpy(node_registry.default_device, "/dev/ttyUSB0"); // Default
        if (json_object_object_get_ex(system_obj, "startup_clear_duration", &temp_obj))
            node_registry.startup_clear_duration = json_object_get_int(temp_obj);
        if (json_object_object_get_ex(system_obj, "detection_interval", &temp_obj))
            node_registry.detection_interval = json_object_get_int(temp_obj);
        else
            node_registry.detection_interval = 60; // Default 60 seconds
        if (json_object_object_get_ex(system_obj, "detection_timeout", &temp_obj))
            node_registry.detection_timeout = json_object_get_int(temp_obj);
        else
            node_registry.detection_timeout = 30; // Default 30 seconds
    }
    
    // Parse system info
    json_object *system_info_obj;
    if (json_object_object_get_ex(effective_root, "system_info", &system_info_obj)) {
        json_object *temp_obj;
        if (json_object_object_get_ex(system_info_obj, "firmware_version", &temp_obj))
            safe_strncpy(node_registry.system_info.firmware_version, json_object_get_string(temp_obj), sizeof(node_registry.system_info.firmware_version));
        if (json_object_object_get_ex(system_info_obj, "device_type", &temp_obj))
            safe_strncpy(node_registry.system_info.device_type, json_object_get_string(temp_obj), sizeof(node_registry.system_info.device_type));
        if (json_object_object_get_ex(system_info_obj, "manufacturer", &temp_obj))
            safe_strncpy(node_registry.system_info.manufacturer, json_object_get_string(temp_obj), sizeof(node_registry.system_info.manufacturer));
        if (json_object_object_get_ex(system_info_obj, "model", &temp_obj))
            safe_strncpy(node_registry.system_info.model, json_object_get_string(temp_obj), sizeof(node_registry.system_info.model));
    }
    
    // Parse UART config
    json_object *uart_obj;
    if (json_object_object_get_ex(effective_root, "uart_config", &uart_obj)) {
        json_object *temp_obj;
        if (json_object_object_get_ex(uart_obj, "config_file_path", &temp_obj))
            safe_strncpy(node_registry.uart_config.config_file_path, json_object_get_string(temp_obj), sizeof(node_registry.uart_config.config_file_path));
        
        json_object *buffer_sizes_obj;
        if (json_object_object_get_ex(uart_obj, "buffer_sizes", &buffer_sizes_obj)) {
            if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer", &temp_obj))
                node_registry.uart_config.response_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
                node_registry.uart_config.temp_buffer_size = json_object_get_int(temp_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer", &temp_obj))
                node_registry.uart_config.error_message_buffer_size = json_object_get_int(temp_obj);
        }
        
        json_object *timing_obj;
        if (json_object_object_get_ex(uart_obj, "timing", &timing_obj)) {
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
    
    // Parse MQTT config
    json_object *mqtt_obj;
    if (json_object_object_get_ex(effective_root, "mqtt_config", &mqtt_obj)) {
        json_object *temp_obj;
        if (json_object_object_get_ex(mqtt_obj, "broker_host", &temp_obj))
            safe_strncpy(node_registry.mqtt_config.broker_host, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.broker_host));
        if (json_object_object_get_ex(mqtt_obj, "client_id", &temp_obj))
            safe_strncpy(node_registry.mqtt_config.client_id, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.client_id));
        if (json_object_object_get_ex(mqtt_obj, "username", &temp_obj))
            safe_strncpy(node_registry.mqtt_config.username, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.username));
        if (json_object_object_get_ex(mqtt_obj, "password", &temp_obj))
            safe_strncpy(node_registry.mqtt_config.password, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.password));
        if (json_object_object_get_ex(mqtt_obj, "topic_telemetry", &temp_obj))
            safe_strncpy(node_registry.mqtt_config.topic_telemetry, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.topic_telemetry));
        if (json_object_object_get_ex(mqtt_obj, "topic_attributes", &temp_obj))
           safe_strncpy(node_registry.mqtt_config.topic_attributes, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.topic_attributes));
        if (json_object_object_get_ex(mqtt_obj, "topic_control", &temp_obj))
           safe_strncpy(node_registry.mqtt_config.topic_control, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.topic_control));
        if (json_object_object_get_ex(mqtt_obj, "topic_status", &temp_obj))
           safe_strncpy(node_registry.mqtt_config.topic_status, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.topic_status));
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
        
        json_object *system_fields_obj;
        if (json_object_object_get_ex(mqtt_obj, "system_fields", &system_fields_obj)) {
           if (json_object_object_get_ex(system_fields_obj, "data_source", &temp_obj))
              safe_strncpy(node_registry.mqtt_config.system_fields.data_source, json_object_get_string(temp_obj), sizeof(node_registry.mqtt_config.system_fields.data_source));
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
        json_object_object_get_ex(effective_root, "node", &nodes_array)) {
        int array_len = json_object_array_length(nodes_array);
        if (array_len > 0) {
            #ifdef DEBUG
            printf("CHECKPOINT: Allocating memory for %d nodes\n", array_len);
            #endif
            node_registry.nodes = malloc(sizeof(node_config_t) * array_len);
            if (!node_registry.nodes) {
                #ifdef DEBUG
                printf("ERROR: Failed to allocate memory for nodes\n");
                #endif
                json_object_put(root);
                return -1;
            }
            node_registry.capacity = array_len;
            node_registry.count = 0;
            
            for (int i = 0; i < array_len; i++) {
                json_object *node_obj = json_object_array_get_idx(nodes_array, i);
                #ifdef DEBUG
                printf("CHECKPOINT: Parsing node at index %d\n", i);
                #endif
                if (parse_node_config(node_obj, &node_registry.nodes[i]) == 0) {
                    node_registry.count++;
                    #ifdef DEBUG
                    printf("CHECKPOINT: Successfully parsed node %d\n", i);
                    #endif
                } else {
                    #ifdef DEBUG
                    printf("WARNING: Failed to parse node at index %d\n", i);
                    #endif
                }
            }
        }
    }
    
    json_object_put(root);
    
    #ifdef DEBUG
    printf("CHECKPOINT: Configuration loaded successfully\n");
    printf("Loaded nodes: %d\n", node_registry.count);
    printf("Detection interval: %d seconds\n", node_registry.detection_interval);
    printf("MQTT broker: %s:%d\n", node_registry.mqtt_config.broker_host, node_registry.mqtt_config.broker_port);
    #endif
    
    return 0;
}

// Start node detection sequence
int start_node_detection(void) {
    #ifdef DEBUG
    printf("Starting node detection sequence\n");
    #endif
    for (int i = 0; i < node_registry.count; i++) {
        node_config_t *node = &node_registry.nodes[i];
        if (detect_node_type(node) == 0) {
            node->detected = 1;
            node->last_detection = time(NULL);
            #ifdef DEBUG
            printf("Node %d (%s) detected successfully\n", node->node_id, node->name);
            #endif
        } else {
            node->detected = 0;
            #ifdef DEBUG
            printf("Node %d (%s) not detected\n", node->node_id, node->name);
            #endif
        }
    }
    return 0;
}

// Improved detect_node_type with better error handling
int detect_node_type(node_config_t *node) {
    if (!node) {
        #ifdef DEBUG
        printf("ERROR: detect_node_type called with NULL node\n");
        #endif
        return -1;
    }
    
    // If no detection commands, assume node is detected (fallback)
    if (!node->detection_commands || node->detection_count == 0) {
        #ifdef DEBUG
        printf("Node %d (%s): No detection commands, assuming detected\n", node->node_id, node->name);
        #endif
        return 0;
    }
    
    #ifdef DEBUG
    printf("Detecting node %d (%s) with %d commands\n", node->node_id, node->name, node->detection_count);
    #endif
    
    for (int i = 0; i < node->detection_count; i++) {
        detection_cmd_t *cmd = &node->detection_commands[i];
        if (!cmd) continue;
        
        #ifdef DEBUG
        printf("Sending detection command 0x%02X for %s\n", cmd->command, cmd->description);
        #endif
        
        // Simulate detection (in real implementation, send UART command)
        usleep(cmd->timeout_ms * 1000);
    }
    
    return 0; // Assume success for now
}

// Standard getter functions with NULL checks
node_config_t *get_node_by_id(int node_id) {
    for (int i = 0; i < node_registry.count; i++) {
        if (node_registry.nodes[i].node_id == node_id) {
            return &node_registry.nodes[i];
        }
    }
    return NULL;
}

node_config_t *get_node_by_index(int index) {
    if (index >= 0 && index < node_registry.count) {
        return &node_registry.nodes[index];
    }
    return NULL;
}

int get_node_count(void) {
    return node_registry.count;
}

menu_item_t *get_menu_item_by_cmd(node_config_t *node, int cmd) {
    if (!node || !node->menu_items)
        return NULL;
    for (int i = 0; i < node->menu_count; i++) {
        if (node->menu_items[i].cmd == cmd)
            return &node->menu_items[i];
    }
    return NULL;
}

// System config getters with defaults
int get_auto_read_command_id(void) { return node_registry.auto_read_command_id; }
int get_uart_clear_timeout(void) { return node_registry.uart_clear_timeout; }
int get_ui_refresh_delay(void) { return node_registry.ui_refresh_delay > 0 ? node_registry.ui_refresh_delay : 1000; }
int get_uart_wait_timeout(void) { return node_registry.uart_wait_timeout; }
int get_default_baudrate(void) { return node_registry.default_baudrate > 0 ? node_registry.default_baudrate : 115200; }
const char *get_default_device(void) { return strlen(node_registry.default_device) > 0 ? node_registry.default_device : "/dev/ttyUSB0"; }
int get_startup_clear_duration(void) { return node_registry.startup_clear_duration; }

// Detection getters with defaults
int get_detection_interval(void) { return node_registry.detection_interval > 0 ? node_registry.detection_interval : 60; }
int get_detection_timeout(void) { return node_registry.detection_timeout > 0 ? node_registry.detection_timeout : 30; }

system_info_t *get_system_info(void) { return &node_registry.system_info; }
uart_config_t *get_uart_config(void) { return &node_registry.uart_config; }
const char *get_uart_config_file_path(void) { return node_registry.uart_config.config_file_path; }
mqtt_config_t *get_mqtt_config(void) { return &node_registry.mqtt_config; }

// Control queue functions with safety
int add_control_command(int node_id, int cmd_id, const char *params) {
    pthread_mutex_lock(&node_registry.control_queue.mutex);
    if (node_registry.control_queue.count >= 100) {
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
        safe_strncpy(cmd->params, params, sizeof(cmd->params));
    else
        cmd->params[0] = '\0';
    cmd->timestamp = time(NULL);
    cmd->processed = 0;
    node_registry.control_queue.tail = (node_registry.control_queue.tail + 1) % 100;
    node_registry.control_queue.count++;
    pthread_cond_signal(&node_registry.control_queue.cond);
    pthread_mutex_unlock(&node_registry.control_queue.mutex);
    return 0;
}

int get_control_command(server_control_cmd_t *cmd) {
    if (!cmd)
        return -1;
    pthread_mutex_lock(&node_registry.control_queue.mutex);
    if (node_registry.control_queue.count == 0) {
        pthread_mutex_unlock(&node_registry.control_queue.mutex);
        return -1;
    }
    *cmd = node_registry.control_queue.commands[node_registry.control_queue.head];
    node_registry.control_queue.head = (node_registry.control_queue.head + 1) % 100;
    node_registry.control_queue.count--;
    pthread_mutex_unlock(&node_registry.control_queue.mutex);
    return 0;
}

control_queue_t *get_control_queue(void) {
    return &node_registry.control_queue;
}

void set_config_mode(int enabled) {
    node_registry.config_mode = enabled;
}

int get_config_mode(void) {
    return node_registry.config_mode;
}

communication_type_t get_communication_type(void) {
    return node_registry.communication_type;
}

void set_communication_type(communication_type_t type) {
    if (type >= 0 && type < COMM_TYPE_COUNT)
        node_registry.communication_type = type;
}

const char *get_communication_type_name(communication_type_t type) {
    switch(type) {
        case COMM_TYPE_MQTT: return "MQTT";
        case COMM_TYPE_HTTP: return "HTTP";
        case COMM_TYPE_WEBSOCKET: return "WebSocket";
        case COMM_TYPE_TCP: return "TCP";
        default: return "Unknown";
    }
}

// Safe cleanup all resources
void cleanup_nodes_config(void) {
    #ifdef DEBUG
    printf("Starting cleanup of node registry\n");
    #endif
    
    // Clean up each node safely
    for (int i = 0; i < node_registry.count; i++) {
        node_config_t *node = &node_registry.nodes[i];
        if (!node) continue;
        
        // Safe cleanup menu_items
        if (node->menu_items) {
            #ifdef DEBUG
            printf("Freeing menu_items for node %d\n", i);
            #endif
            free(node->menu_items);
            node->menu_items = NULL;
        }
        
        // Safe cleanup detection_commands
        if (node->detection_commands) {
            #ifdef DEBUG
            printf("Freeing detection_commands for node %d\n", i);
            #endif
            free(node->detection_commands);
            node->detection_commands = NULL;
        }
        
        // Safe cleanup mqtt_data
        if (node->mqtt_data) {
            #ifdef DEBUG
            printf("Freeing mqtt_data for node %d\n", i);
            #endif
            free_shared_data(node->mqtt_data);
            node->mqtt_data = NULL;
        }
    }
    
    // Safe cleanup main nodes array
    if (node_registry.nodes) {
        #ifdef DEBUG
        printf("Freeing main nodes array\n");
        #endif
        free(node_registry.nodes);
        node_registry.nodes = NULL;
    }
    
    // Safe cleanup other arrays
    if (node_registry.uart_config.supported_baudrates) {
        free(node_registry.uart_config.supported_baudrates);
        node_registry.uart_config.supported_baudrates = NULL;
    }
    
    // Cleanup mutexes/conditions safely
    pthread_mutex_destroy(&node_registry.control_queue.mutex);
    pthread_cond_destroy(&node_registry.control_queue.cond);
    
    // Zero out the registry
    memset(&node_registry, 0, sizeof(node_registry));
    
    #ifdef DEBUG
    printf("Configuration cleanup completed safely\n");
    #endif
}
