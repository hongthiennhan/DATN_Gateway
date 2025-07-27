#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <pthread.h>
#include <time.h>

#define MAX_NODES 10
#define MAX_MENU_ITEMS 20
#define MAX_NODE_TYPES 10

// Forward declaration
typedef struct shared_data_s shared_data_t;

typedef struct {
    int cmd;
    char label[64];
    char uart_cmd[32];
    char hex_value[16];  // NEW: Store hex value as string
    int timeout_ms;
} menu_item_t;

typedef struct {
    char broker_host[128];
    int broker_port;
    char client_id[64];
    char username[128];
    char password[128];
    char topic_telemetry[128];
    char topic_attributes[128];
    int qos;
    int publish_interval;
} mqtt_config_t;

typedef struct {
    int node_id;
    char name[128];
    char type[64];
    menu_item_t *menu_items;
    int menu_count;
    int auto_read_cmd;
    int auto_read_interval;
    char mqtt_topic[64];
    char data_structure[64];
    char reflash_script[512];
    
    // Runtime data
    shared_data_t *mqtt_data;
} node_config_t;

typedef struct {
    node_config_t *nodes;
    int count;
    int capacity;
    
    // System config
    int auto_read_command_id;
    int uart_clear_timeout;
    int ui_refresh_delay;
    int uart_wait_timeout;
    
    // MQTT config
    mqtt_config_t mqtt_config;
} node_registry_t;

// API functions
int load_nodes_config(const char *config_file);
node_config_t* get_node_by_id(int node_id);
node_config_t* get_node_by_index(int index);
int get_node_count(void);
menu_item_t* get_menu_item_by_cmd(node_config_t *node, int cmd);
void cleanup_nodes_config(void);

// System config getters
int get_auto_read_command_id(void);
int get_uart_clear_timeout(void);
int get_ui_refresh_delay(void);
int get_uart_wait_timeout(void);

// MQTT config getters
mqtt_config_t* get_mqtt_config(void);

// Utility function
uint32_t hex_string_to_int(const char *hex_str);

#endif
