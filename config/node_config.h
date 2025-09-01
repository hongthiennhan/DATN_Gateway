#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include "main.h"
#define MAX_NODES 10
#define MAX_MENU_ITEMS 20
#define MAX_DETECTION_COMMANDS 10

// Config download settings
#define MAX_JSON_SIZE (1024 * 1024)
#define CONFIG_DIR ".."
#define CONFIG_FILE "config.json"
#define FALLBACK_CONFIG_FILE "nodes_config.json"

typedef enum {
    COMM_TYPE_MQTT = 0,
    COMM_TYPE_HTTP,     // For HTTP communication (not implemented yet)
    COMM_TYPE_WEBSOCKET, // For WebSocket communication (not implemented yet)
    COMM_TYPE_TCP,      // For direct TCP communication (not implemented yet)
    COMM_TYPE_COUNT
} communication_type_t;

// Raw data structure for communication
typedef struct {
    unsigned char *data;
    int length;
    time_t timestamp; // Timestamp when data was received
} raw_data_t;

// Forward declaration
typedef struct {
    void* data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

typedef struct {
    bool is_paused;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} thread_pause_t;

// Node detection command structure
typedef struct {
    char command[64];
    char expected_response[64];
    uint16_t timeout_ms;
    char description[128];
} detection_cmd_t;

// Control command from server
typedef struct {
    uint32_t node_id;
    uint32_t cmd_id;
    char params[256];
    time_t timestamp;
    uint8_t processed;
} server_control_cmd_t;

// Control command queue
typedef struct {
    server_control_cmd_t commands[100];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} control_queue_t;

typedef struct {
    uint8_t cmd;
    char label[64];
    char hex_value[256];
    uint16_t timeout_ms;
    uint8_t is_direct;
} menu_item_t;

typedef struct {
    char firmware_version[32];
    char device_type[64];
    char manufacturer[64];
    char model[32];
} system_info_t;

typedef struct {
    char data_source[64];
    uint8_t include_timestamp;
    uint8_t include_gateway_ip;
    uint8_t include_node_count;
} system_fields_t;

typedef struct {
    char broker_host[128];
    uint16_t broker_port;
    char client_id[64];
    char username[128];
    char password[128];
    char topic_telemetry[128];
    char topic_attributes[128];
    char topic_control[128];
    char topic_status[128];
    uint8_t qos;
    uint16_t publish_interval;
    uint16_t connection_timeout;
    uint16_t reconnect_delay_ms;
    uint16_t loop_interval_ms;
    uint16_t payload_buffer_size;
    uint16_t attributes_buffer_size;
    system_fields_t system_fields;
} mqtt_config_t;

//Add TCP parse struct:
typedef struct {
    uint8_t keepalive;
    uint16_t keepalive_idle;
    uint16_t keepalive_interval;
    uint8_t keepalive_count;
    uint8_t tcp_nodelay;
} tcp_socket_options_t;

typedef struct {
    char data_source[64];
    uint8_t include_timestamp;
    uint8_t include_gateway_ip; 
    uint8_t include_node_count; 
    uint8_t include_system_info;
} tcp_system_fields_t;

typedef struct {
    char message_delimiter[8];
    uint16_t max_message_size;
    uint8_t compression_enabled;
    uint8_t encryption_enabled; 
} tcp_protocol_settings_t;

typedef struct {
    uint8_t config_download;  
    uint8_t control_commands; 
    uint8_t telemetry_upload; 
    uint8_t status_reporting; 
} tcp_features_t;

typedef struct {
    char server_host[128];
    uint16_t server_port;
    char config_server_host[128];
    uint16_t config_server_port;
    char client_id[64];
    char protocol_version[16];
    char data_format[16];
    uint16_t send_interval;
    uint16_t connection_timeout;
    uint16_t reconnect_delay_ms;
    uint16_t loop_interval_ms;
    uint16_t payload_buffer_size;
    uint16_t receive_buffer_size;
    tcp_socket_options_t socket_options;
    tcp_system_fields_t system_fields;
    tcp_protocol_settings_t protocol_settings;
    tcp_features_t features;
} tcp_config_t;



typedef struct {
    uint32_t rate;
    char speed_code[16];
} baudrate_mapping_t;

typedef struct {
    uint16_t response_buffer_size;
    uint16_t temp_buffer_size;
    uint16_t error_message_buffer_size;
    uint16_t poll_interval_ms;
    uint16_t flush_interval_ms;
    baudrate_mapping_t *supported_baudrates;
    uint8_t baudrate_count;
    uint32_t default_baudrate_fallback;
} uart_config_t;

typedef struct {
    uint16_t response_buffer_size;
    uint16_t temp_buffer_size;
    uint16_t error_message_buffer_size;
    uint16_t poll_interval_ms;
    baudrate_mapping_t *supported_baudrates;
    uint8_t baudrate_count;
    uint32_t default_baudrate_fallback;
} modbus_config_t;

typedef struct {
    uint16_t response_buffer_size;
    uint16_t temp_buffer_size;
    uint16_t error_message_buffer_size;
    uint16_t poll_interval_ms;
    baudrate_mapping_t *supported_baudrates;
    uint8_t baudrate_count;
    uint32_t default_baudrate_fallback;
} can_config_t;

typedef struct {
    uint32_t node_id;
    char address[8];
    char name[64];
    char com_type[64];
    menu_item_t *menu_items;
    uint32_t menu_count;
    char data_format[16];
    char reflash_script[256];
    
    // NEW: Node detection commands
    detection_cmd_t *detection_commands;
    uint32_t detection_count;
    uint8_t detected;              // 1 if node detected, 0 otherwise
    time_t last_detection;     // Last detection attempt timestamp
    time_t last_data_received; // when node last sent data

    // Runtime data
    shared_data_t *mqtt_data;
} node_config_t;

typedef struct {
    node_config_t *nodes;
    uint32_t count;
    uint32_t capacity;

    // System config
    uint32_t ui_refresh_delay;
    uint32_t default_baudrate;
    char default_device[256];
    uint32_t startup_clear_duration;
    // Communication type
    communication_type_t communication_type;

    // System info
    system_info_t system_info;
    
    // UART config
    uart_config_t uart_config;

    // Modbus config
    modbus_config_t modbus_config;

    // CAN config
    can_config_t can_config;

    // MQTT config
    mqtt_config_t mqtt_config;
    
    // Control queue
    control_queue_t control_queue;
    
} node_registry_t;

// API functions
int load_nodes_config(const char *config_file);
node_config_t* get_node_by_id(uint32_t node_id);
node_config_t* get_node_by_index(uint32_t index);
int get_node_count(void);
menu_item_t* get_menu_item_by_cmd(node_config_t *node, uint32_t cmd);
void cleanup_nodes_config(void);

// System config getters
int get_ui_refresh_delay(void);
int get_default_baudrate(void);
const char* get_default_device(void);
int get_startup_clear_duration(void);

// System info getters
system_info_t* get_system_info(void);

// UART config getters
uart_config_t* get_uart_config(void);

// Modbus config getters
modbus_config_t* get_modbus_config(void);

// CAN config getters
can_config_t* get_can_config(void);

// MQTT config getters
mqtt_config_t* get_mqtt_config(void);

// Control queue functions
int add_control_command(uint32_t node_id, uint32_t cmd_id, const char *params);
int get_control_command(server_control_cmd_t *cmd);

//Communication type functions
communication_type_t get_communication_type(void);
void set_communication_type(communication_type_t type);
const char* get_communication_type_name(communication_type_t type);

// Mutex for config file:
extern pthread_mutex_t config_mutex;
extern volatile int config_reloading;

// Thread-safe functions
int safe_get_node_count(void);
int safe_reload_config(void);
node_config_t *safe_get_node_by_index(uint32_t index);

#endif
