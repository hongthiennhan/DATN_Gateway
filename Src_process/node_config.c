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
        
        if (json_object_object_get_ex(system_config_obj, "auto_read_command_id", &auto_read_cmd_obj))
            node_registry.auto_read_command_id = json_object_get_int(auto_read_cmd_obj);
        if (json_object_object_get_ex(system_config_obj, "uart_clear_timeout", &uart_clear_obj))
            node_registry.uart_clear_timeout = json_object_get_int(uart_clear_obj);
        if (json_object_object_get_ex(system_config_obj, "ui_refresh_delay", &ui_refresh_obj))
            node_registry.ui_refresh_delay = json_object_get_int(ui_refresh_obj);
        if (json_object_object_get_ex(system_config_obj, "uart_wait_timeout", &uart_wait_obj))
            node_registry.uart_wait_timeout = json_object_get_int(uart_wait_obj);
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
        json_object *mqtt_topic_obj, *data_structure_obj, *reflash_script_obj;
        
        json_object_object_get_ex(node_obj, "id", &id_obj);
        json_object_object_get_ex(node_obj, "name", &name_obj);
        json_object_object_get_ex(node_obj, "type", &type_obj);
        json_object_object_get_ex(node_obj, "auto_read_cmd", &auto_read_cmd_obj);
        json_object_object_get_ex(node_obj, "auto_read_interval", &auto_read_interval_obj);
        json_object_object_get_ex(node_obj, "mqtt_topic", &mqtt_topic_obj);
        json_object_object_get_ex(node_obj, "data_structure", &data_structure_obj);
        json_object_object_get_ex(node_obj, "reflash_script", &reflash_script_obj);
        
        node->node_id = json_object_get_int(id_obj);
        strcpy(node->name, json_object_get_string(name_obj));
        strcpy(node->type, json_object_get_string(type_obj));
        node->auto_read_cmd = json_object_get_int(auto_read_cmd_obj);
        node->auto_read_interval = json_object_get_int(auto_read_interval_obj);
        strcpy(node->mqtt_topic, json_object_get_string(mqtt_topic_obj));
        strcpy(node->data_structure, json_object_get_string(data_structure_obj));
        strcpy(node->reflash_script, json_object_get_string(reflash_script_obj));
        
        // Parse menu items
        json_object *menu_array;
        json_object_object_get_ex(node_obj, "menu_items", &menu_array);
        int menu_len = json_object_array_length(menu_array);
        
        node->menu_items = malloc(sizeof(menu_item_t) * menu_len);
        node->menu_count = menu_len;
        
        for (int j = 0; j < menu_len; j++) {
            json_object *menu_item_obj = json_object_array_get_idx(menu_array, j);
            menu_item_t *menu_item = &node->menu_items[j];
            
            json_object *cmd_obj, *label_obj, *uart_cmd_obj, *timeout_obj;
            json_object_object_get_ex(menu_item_obj, "cmd", &cmd_obj);
            json_object_object_get_ex(menu_item_obj, "label", &label_obj);
            json_object_object_get_ex(menu_item_obj, "uart_cmd", &uart_cmd_obj);
            json_object_object_get_ex(menu_item_obj, "timeout_ms", &timeout_obj);
            
            menu_item->cmd = json_object_get_int(cmd_obj);
            strcpy(menu_item->label, json_object_get_string(label_obj));
            strcpy(menu_item->uart_cmd, json_object_get_string(uart_cmd_obj));
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

void cleanup_nodes_config(void) {
    for (int i = 0; i < node_registry.count; i++) {
        free(node_registry.nodes[i].menu_items);
        if (node_registry.nodes[i].mqtt_data) {
            pthread_mutex_destroy(&node_registry.nodes[i].mqtt_data->mutex);
            pthread_cond_destroy(&node_registry.nodes[i].mqtt_data->cond);
            free(node_registry.nodes[i].mqtt_data);
        }
    }
    free(node_registry.nodes);
    node_registry.count = 0;
}
