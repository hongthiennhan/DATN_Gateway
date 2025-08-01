#include "main.h"
#include "node_config.h"
#include "thread_func.h"

static node_registry_t node_registry = {0};

int load_nodes_config(const char *config_file) {
    json_object *root = json_object_from_file(config_file);
    if (!root) {
        printf("Error: Cannot load config file %s\n", config_file);
        return -1;
    }
    
    // Parse system config
    json_object *system_config_obj;
    if (json_object_object_get_ex(root, "system_config", &system_config_obj)) {
        json_object *auto_read_cmd_obj, *uart_clear_obj, *ui_refresh_obj, *uart_wait_obj;
        json_object *default_baudrate_obj, *default_device_obj, *startup_clear_obj;
        
        if (json_object_object_get_ex(system_config_obj, "auto_read_command_id", &auto_read_cmd_obj))
            node_registry.auto_read_command_id = json_object_get_int(auto_read_cmd_obj);
        if (json_object_object_get_ex(system_config_obj, "uart_clear_timeout", &uart_clear_obj))
            node_registry.uart_clear_timeout = json_object_get_int(uart_clear_obj);
        if (json_object_object_get_ex(system_config_obj, "ui_refresh_delay", &ui_refresh_obj))
            node_registry.ui_refresh_delay = json_object_get_int(ui_refresh_obj);
        if (json_object_object_get_ex(system_config_obj, "uart_wait_timeout", &uart_wait_obj))
            node_registry.uart_wait_timeout = json_object_get_int(uart_wait_obj);
        if (json_object_object_get_ex(system_config_obj, "default_baudrate", &default_baudrate_obj))
            node_registry.default_baudrate = json_object_get_int(default_baudrate_obj);
        if (json_object_object_get_ex(system_config_obj, "default_device", &default_device_obj))
            strcpy(node_registry.default_device, json_object_get_string(default_device_obj));
        if (json_object_object_get_ex(system_config_obj, "startup_clear_duration", &startup_clear_obj))
            node_registry.startup_clear_duration = json_object_get_int(startup_clear_obj);
    }
    
    // NEW: Parse system_info config
    json_object *system_info_obj;
    if (json_object_object_get_ex(root, "system_info", &system_info_obj)) {
        json_object *firmware_obj, *device_type_obj, *manufacturer_obj, *model_obj;
        
        if (json_object_object_get_ex(system_info_obj, "firmware_version", &firmware_obj))
            strcpy(node_registry.system_info.firmware_version, json_object_get_string(firmware_obj));
        if (json_object_object_get_ex(system_info_obj, "device_type", &device_type_obj))
            strcpy(node_registry.system_info.device_type, json_object_get_string(device_type_obj));
        if (json_object_object_get_ex(system_info_obj, "manufacturer", &manufacturer_obj))
            strcpy(node_registry.system_info.manufacturer, json_object_get_string(manufacturer_obj));
        if (json_object_object_get_ex(system_info_obj, "model", &model_obj))
            strcpy(node_registry.system_info.model, json_object_get_string(model_obj));
    }
    
    // Parse UART config
    json_object *uart_config_obj;
    if (json_object_object_get_ex(root, "uart_config", &uart_config_obj)) {
        json_object *config_file_obj, *buffer_sizes_obj, *timing_obj, *supported_baudrates_obj, *fallback_obj;
        
        if (json_object_object_get_ex(uart_config_obj, "config_file_path", &config_file_obj))
            strcpy(node_registry.uart_config.config_file_path, json_object_get_string(config_file_obj));
        
        if (json_object_object_get_ex(uart_config_obj, "buffer_sizes", &buffer_sizes_obj)) {
            json_object *resp_buf_obj, *temp_buf_obj, *err_buf_obj;
            if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer", &resp_buf_obj))
                node_registry.uart_config.response_buffer_size = json_object_get_int(resp_buf_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_buf_obj))
                node_registry.uart_config.temp_buffer_size = json_object_get_int(temp_buf_obj);
            if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer", &err_buf_obj))
                node_registry.uart_config.error_message_buffer_size = json_object_get_int(err_buf_obj);
        }
        
        if (json_object_object_get_ex(uart_config_obj, "timing", &timing_obj)) {
            json_object *poll_obj, *flush_obj, *select_obj;
            if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &poll_obj))
                node_registry.uart_config.poll_interval_ms = json_object_get_int(poll_obj);
            if (json_object_object_get_ex(timing_obj, "flush_interval_ms", &flush_obj))
                node_registry.uart_config.flush_interval_ms = json_object_get_int(flush_obj);
            if (json_object_object_get_ex(timing_obj, "select_timeout_ms", &select_obj))
                node_registry.uart_config.select_timeout_ms = json_object_get_int(select_obj);
        }
        
        if (json_object_object_get_ex(uart_config_obj, "default_baudrate_fallback", &fallback_obj))
            node_registry.uart_config.default_baudrate_fallback = json_object_get_int(fallback_obj);
        
        // Parse supported baudrates
        if (json_object_object_get_ex(uart_config_obj, "supported_baudrates", &supported_baudrates_obj)) {
            int baudrate_count = json_object_array_length(supported_baudrates_obj);
            node_registry.uart_config.supported_baudrates = malloc(sizeof(baudrate_mapping_t) * baudrate_count);
            node_registry.uart_config.baudrate_count = baudrate_count;
            
            for (int i = 0; i < baudrate_count; i++) {
                json_object *baudrate_obj = json_object_array_get_idx(supported_baudrates_obj, i);
                json_object *rate_obj, *speed_code_obj;
                
                if (json_object_object_get_ex(baudrate_obj, "rate", &rate_obj))
                    node_registry.uart_config.supported_baudrates[i].rate = json_object_get_int(rate_obj);
                if (json_object_object_get_ex(baudrate_obj, "speed_code", &speed_code_obj))
                    strcpy(node_registry.uart_config.supported_baudrates[i].speed_code, json_object_get_string(speed_code_obj));
            }
        }
    }
    
    // UPDATED: Parse MQTT config with new fields
    json_object *mqtt_config_obj;
    if (json_object_object_get_ex(root, "mqtt_config", &mqtt_config_obj)) {
        json_object *broker_host_obj, *broker_port_obj, *client_id_obj, *username_obj, *password_obj;
        json_object *topic_telemetry_obj, *topic_attributes_obj, *qos_obj, *publish_interval_obj;
        json_object *connection_timeout_obj, *reconnect_delay_obj, *loop_interval_obj;
        json_object *payload_buffer_obj, *attributes_buffer_obj, *system_fields_obj;
        
        if (json_object_object_get_ex(mqtt_config_obj, "broker_host", &broker_host_obj))
            strcpy(node_registry.mqtt_config.broker_host, json_object_get_string(broker_host_obj));
        if (json_object_object_get_ex(mqtt_config_obj, "broker_port", &broker_port_obj))
            node_registry.mqtt_config.broker_port = json_object_get_int(broker_port_obj);
        if (json_object_object_get_ex(mqtt_config_obj, "client_id", &client_id_obj))
            strcpy(node_registry.mqtt_config.client_id, json_object_get_string(client_id_obj));
        if (json_object_object_get_ex(mqtt_config_obj, "username", &username_obj))
            strcpy(node_registry.mqtt_config.username, json_object_get_string(username_obj));
        if (json_object_object_get_ex(mqtt_config_obj, "password", &password_obj))
            strcpy(node_registry.mqtt_config.password, json_object_get_string(password_obj));
        if (json_object_object_get_ex(mqtt_config_obj, "topic_telemetry", &topic_telemetry_obj))
            strcpy(node_registry.mqtt_config.topic_telemetry, json_object_get_string(topic_telemetry_obj));
        if (json_object_object_get_ex(mqtt_config_obj, "topic_attributes", &topic_attributes_obj))
            strcpy(node_registry.mqtt_config.topic_attributes, json_object_get_string(topic_attributes_obj));
        if (json_object_object_get_ex(mqtt_config_obj, "qos", &qos_obj))
            node_registry.mqtt_config.qos = json_object_get_int(qos_obj);
        if (json_object_object_get_ex(mqtt_config_obj, "publish_interval", &publish_interval_obj))
            node_registry.mqtt_config.publish_interval = json_object_get_int(publish_interval_obj);
        
        // NEW: Parse additional MQTT config fields
        if (json_object_object_get_ex(mqtt_config_obj, "connection_timeout", &connection_timeout_obj))
            node_registry.mqtt_config.connection_timeout = json_object_get_int(connection_timeout_obj);
        if (json_object_object_get_ex(mqtt_config_obj, "reconnect_delay_ms", &reconnect_delay_obj))
            node_registry.mqtt_config.reconnect_delay_ms = json_object_get_int(reconnect_delay_obj);
        if (json_object_object_get_ex(mqtt_config_obj, "loop_interval_ms", &loop_interval_obj))
            node_registry.mqtt_config.loop_interval_ms = json_object_get_int(loop_interval_obj);
        if (json_object_object_get_ex(mqtt_config_obj, "payload_buffer_size", &payload_buffer_obj))
            node_registry.mqtt_config.payload_buffer_size = json_object_get_int(payload_buffer_obj);
        if (json_object_object_get_ex(mqtt_config_obj, "attributes_buffer_size", &attributes_buffer_obj))
            node_registry.mqtt_config.attributes_buffer_size = json_object_get_int(attributes_buffer_obj);
        
        // Parse system_fields
        if (json_object_object_get_ex(mqtt_config_obj, "system_fields", &system_fields_obj)) {
            json_object *data_source_obj, *include_timestamp_obj, *include_gateway_ip_obj, *include_node_count_obj;
            
            if (json_object_object_get_ex(system_fields_obj, "data_source", &data_source_obj))
                strcpy(node_registry.mqtt_config.system_fields.data_source, json_object_get_string(data_source_obj));
            if (json_object_object_get_ex(system_fields_obj, "include_timestamp", &include_timestamp_obj))
                node_registry.mqtt_config.system_fields.include_timestamp = json_object_get_boolean(include_timestamp_obj);
            if (json_object_object_get_ex(system_fields_obj, "include_gateway_ip", &include_gateway_ip_obj))
                node_registry.mqtt_config.system_fields.include_gateway_ip = json_object_get_boolean(include_gateway_ip_obj);
            if (json_object_object_get_ex(system_fields_obj, "include_node_count", &include_node_count_obj))
                node_registry.mqtt_config.system_fields.include_node_count = json_object_get_boolean(include_node_count_obj);
        }
    }
    
    json_object *nodes_array;
    if (!json_object_object_get_ex(root, "nodes", &nodes_array)) {
        printf("Error: No 'nodes' array found in config\n");
        json_object_put(root);
        return -1;
    }
    
    int array_len = json_object_array_length(nodes_array);
    
    // Allocate memory for nodes
    node_registry.nodes = malloc(sizeof(node_config_t) * array_len);
    node_registry.capacity = array_len;
    node_registry.count = 0;
    
    for (int i = 0; i < array_len; i++) {
        json_object *node_obj = json_object_array_get_idx(nodes_array, i);
        node_config_t *node = &node_registry.nodes[i];
        
        // Parse basic node info
        json_object *id_obj, *name_obj, *type_obj, *auto_read_cmd_obj, *auto_read_interval_obj;
        json_object *reflash_script_obj;
        json_object *flash_cmd_obj;
        json_object *expected_data_length_obj, *data_format_obj;
        
        json_object_object_get_ex(node_obj, "id", &id_obj);
        json_object_object_get_ex(node_obj, "name", &name_obj);
        json_object_object_get_ex(node_obj, "type", &type_obj);
        json_object_object_get_ex(node_obj, "auto_read_cmd", &auto_read_cmd_obj);
        json_object_object_get_ex(node_obj, "auto_read_interval", &auto_read_interval_obj);
        json_object_object_get_ex(node_obj, "flash_cmd", &flash_cmd_obj);
        json_object_object_get_ex(node_obj, "reflash_script", &reflash_script_obj);
        json_object_object_get_ex(node_obj, "expected_data_length", &expected_data_length_obj);
        json_object_object_get_ex(node_obj, "data_format", &data_format_obj);
        
        node->node_id = json_object_get_int(id_obj);
        strcpy(node->name, json_object_get_string(name_obj));
        strcpy(node->type, json_object_get_string(type_obj));
        node->auto_read_cmd = json_object_get_int(auto_read_cmd_obj);
        node->auto_read_interval = json_object_get_int(auto_read_interval_obj);
        node->flash_cmd = json_object_get_int(flash_cmd_obj);
        strcpy(node->reflash_script, json_object_get_string(reflash_script_obj));
        
        // NEW: Parse raw data fields
        if (expected_data_length_obj)
            node->expected_data_length = json_object_get_int(expected_data_length_obj);
        else
            node->expected_data_length = 0;
            
        if (data_format_obj)
            strcpy(node->data_format, json_object_get_string(data_format_obj));
        else
            strcpy(node->data_format, "hex");
        
        // Parse menu items
        json_object *menu_array;
        json_object_object_get_ex(node_obj, "menu_items", &menu_array);
        int menu_len = json_object_array_length(menu_array);
        
        node->menu_items = malloc(sizeof(menu_item_t) * menu_len);
        node->menu_count = menu_len;
        
        for (int j = 0; j < menu_len; j++) {
            json_object *menu_item_obj = json_object_array_get_idx(menu_array, j);
            menu_item_t *menu_item = &node->menu_items[j];
            
            json_object *cmd_obj, *label_obj, *uart_cmd_obj, *hex_value_obj, *timeout_obj;
            json_object_object_get_ex(menu_item_obj, "cmd", &cmd_obj);
            json_object_object_get_ex(menu_item_obj, "label", &label_obj);
            json_object_object_get_ex(menu_item_obj, "uart_cmd", &uart_cmd_obj);
            json_object_object_get_ex(menu_item_obj, "hex_value", &hex_value_obj);
            json_object_object_get_ex(menu_item_obj, "timeout_ms", &timeout_obj);
            
            menu_item->cmd = json_object_get_int(cmd_obj);
            strcpy(menu_item->label, json_object_get_string(label_obj));
            strcpy(menu_item->uart_cmd, json_object_get_string(uart_cmd_obj));
            strcpy(menu_item->hex_value, json_object_get_string(hex_value_obj));
            menu_item->timeout_ms = json_object_get_int(timeout_obj);
        }
        
        // Initialize shared data for MQTT
        node->mqtt_data = malloc(sizeof(shared_data_t));
        node->mqtt_data->data = NULL;
        pthread_mutex_init(&node->mqtt_data->mutex, NULL);
        pthread_cond_init(&node->mqtt_data->cond, NULL);
        
        node_registry.count++;
    }
    
    json_object_put(root);
    printf("Loaded %d nodes from config\n", node_registry.count);
    return 0;
}

// Existing functions remain the same
node_config_t* get_node_by_id(int node_id) {
    for (int i = 0; i < node_registry.count; i++) {
        if (node_registry.nodes[i].node_id == node_id) {
            return &node_registry.nodes[i];
        }
    }
    return NULL;
}

node_config_t* get_node_by_index(int index) {
    if (index >= 0 && index < node_registry.count) {
        return &node_registry.nodes[index];
    }
    return NULL;
}

int get_node_count(void) {
    return node_registry.count;
}

menu_item_t* get_menu_item_by_cmd(node_config_t *node, int cmd) {
    if (!node) return NULL;
    
    for (int i = 0; i < node->menu_count; i++) {
        if (node->menu_items[i].cmd == cmd) {
            return &node->menu_items[i];
        }
    }
    return NULL;
}

// System config getters
int get_auto_read_command_id(void) {
    return node_registry.auto_read_command_id;
}

int get_uart_clear_timeout(void) {
    return node_registry.uart_clear_timeout;
}

int get_ui_refresh_delay(void) {
    return node_registry.ui_refresh_delay;
}

int get_uart_wait_timeout(void) {
    return node_registry.uart_wait_timeout;
}

int get_default_baudrate(void) {
    return node_registry.default_baudrate;
}

const char* get_default_device(void) {
    return node_registry.default_device;
}

int get_startup_clear_duration(void) {
    return node_registry.startup_clear_duration;
}

int get_flash_command(void) {
    return node_registry.flash_cmd;
}

// NEW: System info getters
system_info_t* get_system_info(void) {
    return &node_registry.system_info;
}

// UART config getters
uart_config_t* get_uart_config(void) {
    return &node_registry.uart_config;
}

const char* get_uart_config_file_path(void) {
    return node_registry.uart_config.config_file_path;
}

// MQTT config getter
mqtt_config_t* get_mqtt_config(void) {
    return &node_registry.mqtt_config;
}

// Utility function to convert hex string to integer
uint32_t hex_string_to_int(const char *hex_str) {
    if (!hex_str || strlen(hex_str) == 0) return 0;
    
    uint32_t result = 0;
    if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0) {
        sscanf(hex_str, "%x", &result);
    } else {
        sscanf(hex_str, "%x", &result);
    }
    return result;
}

void cleanup_nodes_config(void) {
    for (int i = 0; i < node_registry.count; i++) {
        free(node_registry.nodes[i].menu_items);
        if (node_registry.nodes[i].mqtt_data) {
            pthread_mutex_destroy(&node_registry.nodes[i].mqtt_data->mutex);
            pthread_cond_destroy(&node_registry.nodes[i].mqtt_data->cond);
            free(node_registry.nodes[i].mqtt_data);
        }
    }
    if (node_registry.uart_config.supported_baudrates) {
        free(node_registry.uart_config.supported_baudrates);
    }
    free(node_registry.nodes);
    node_registry.count = 0;
}
