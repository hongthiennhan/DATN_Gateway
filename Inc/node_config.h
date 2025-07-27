#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <pthread.h>
#include <time.h>

#define MAX_NODES 10
#define MAX_MENU_ITEMS 20
#define MAX_NODE_TYPES 10
#define MAX_BAUDRATES 20

// Forward declaration
typedef struct shared_data_s shared_data_t;

typedef struct {
    int cmd;
    char label[64];
    char uart_cmd[32];
    char hex_value[16];
    int timeout_ms;
} menu_item_t;

typedef struct {
    char firmware_version[32];
    char device_type[64];
    char manufacturer[64];
    char model[32];
} system_info_t;

typedef struct {
    char data_source[64];
    int include_timestamp;
    int include_gateway_ip;
    int include_node_count;
} system_fields_t;

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
    int connection_timeout;
    int reconnect_delay_ms;
    int loop_interval_ms;
    int payload_buffer_size;
    int attributes_buffer_size;
    system_fields_t system_fields;
} mqtt_config_t;

typedef struct {
    int rate;
    char speed_code[16];
} baudrate_mapping_t;

typedef struct {
    char config_file_path[256];
    int response_buffer_size;
    int temp_buffer_size;
    int error_message_buffer_size;
    int poll_interval_ms;
    int flush_interval_ms;
    int select_timeout_ms;
    baudrate_mapping_t *supported_baudrates;
    int baudrate_count;
    int default_baudrate_fallback;
} uart_config_t;

// Raw data structure to store binary data without parsing
typedef struct {
    unsigned char *data;
    int length;
} raw_data_t;

typedef struct {
    int node_id;
    char name[128];
    char type[64];
    menu_item_t *menu_items;
    int menu_count;
    int auto_read_cmd;
    int auto_read_interval;
    char mqtt_topic[64];
    char data_structure[64];      // "raw_data" for raw mode
    int expected_data_length;     // Expected raw data length
    char data_format[16];         // "hex" or "base64"
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
    int default_baudrate;
    char default_device[256];
    int startup_clear_duration;
    
    // System info
    system_info_t system_info;
    
    // UART config
    uart_config_t uart_config;
    
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
int get_default_baudrate(void);
const char* get_default_device(void);
int get_startup_clear_duration(void);

// System info getters
system_info_t* get_system_info(void);

// UART config getters
uart_config_t* get_uart_config(void);
const char* get_uart_config_file_path(void);

// MQTT config getters
mqtt_config_t* get_mqtt_config(void);

// Utility function
uint32_t hex_string_to_int(const char *hex_str);

#endif
