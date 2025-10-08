#ifndef GATEWAY_CONFIG_H
#define GATEWAY_CONFIG_H

#include "main.h"

#define MAX_STR_LEN 256
#define MAX_ADDR_LEN 8
#define MAX_HEADER_LEN 8

// Constants
#define CONFIG_DIR ".."
#define CONFIG_FILE "config.json"
#define FALLBACK_CONFIG_FILE "config_backup.json"
#define MAX_JSON_SIZE (64 * 1024)
#define MAX_RECEIVED_DATA_SIZE 2048

// Module configuration structure
typedef struct {
  char command_header[MAX_HEADER_LEN]; // Module command header
  char address[MAX_ADDR_LEN];          // Module address
} module_config_t;

// All gateway modules
typedef struct {
  module_config_t Module_LoRa_E32;     // LoRa E32 module
  module_config_t Module_SIM800;       // SIM800 GSM module
  module_config_t Module_Zigbee;       // Zigbee module
  module_config_t Module_RFID_RC522;   // RFID RC522 module
  module_config_t Module_Display_OLED; // OLED display module
} modules_config_t;

// System configuration
typedef struct {
  uint32_t uart_wait_timeout;       // UART wait timeout in ms
  uint32_t default_baudrate;        // Default baudrate
  char default_device[MAX_STR_LEN]; // Default device path
  uint32_t startup_clear_duration;  // Startup clear duration in ms
  char server_com_type[16];         // Server communication type (e.g., "MQTT",
                                    // "TCP","UDP")
} system_config_t;

// System information
typedef struct {
  char firmware_version[MAX_STR_LEN]; // Firmware version
  char device_type[MAX_STR_LEN];      // Device type
  char manufacturer[MAX_STR_LEN];     // Manufacturer name
  char model[MAX_STR_LEN];            // Device model
} system_info_t;

// Baudrate mapping
typedef struct {
  uint32_t rate;       // Baudrate value
  char speed_code[16]; // Speed code string
} baudrate_mapping_t;

// UART configuration
typedef struct {
  uint16_t response_buffer_size;           // Response buffer size
  uint16_t temp_buffer_size;               // Temp buffer size
  uint16_t error_message_buffer_size;      // Error buffer size
  uint16_t poll_interval_ms;               // Poll interval in ms
  uint16_t flush_interval_ms;              // Flush interval in ms
  baudrate_mapping_t *supported_baudrates; // Supported baudrates array
  uint8_t baudrate_count;                  // Number of baudrates
  uint32_t default_baudrate_fallback;      // Default fallback baudrate
} uart_config_t;

// Modbus configuration
typedef struct {
  uint16_t response_buffer_size;           // Response buffer size
  uint16_t temp_buffer_size;               // Temp buffer size
  uint16_t error_message_buffer_size;      // Error buffer size
  uint16_t poll_interval_ms;               // Poll interval in ms
  baudrate_mapping_t *supported_baudrates; // Supported baudrates array
  uint8_t baudrate_count;                  // Number of baudrates
  uint32_t default_baudrate_fallback;      // Default fallback baudrate
} modbus_config_t;

// CAN configuration
typedef struct {
  uint16_t response_buffer_size;           // Response buffer size
  uint16_t temp_buffer_size;               // Temp buffer size
  uint16_t error_message_buffer_size;      // Error buffer size
  uint16_t poll_interval_ms;               // Poll interval in ms
  baudrate_mapping_t *supported_baudrates; // Supported baudrates array
  uint8_t baudrate_count;                  // Number of baudrates
  uint32_t default_baudrate_fallback;      // Default fallback baudrate
} can_config_t;

// MQTT system fields
typedef struct {
  char data_source[64];       // Data source identifier
  uint8_t include_timestamp;  // Include timestamp flag
  uint8_t include_gateway_ip; // Include gateway IP flag
  uint8_t include_node_count; // Include node count flag
} mqtt_system_fields_t;

// MQTT configuration
typedef struct {
  char broker_host[128];              // MQTT broker host
  uint16_t broker_port;               // MQTT broker port
  char client_id[64];                 // MQTT client ID
  char username[128];                 // MQTT username
  char password[128];                 // MQTT password
  char topic_telemetry[128];          // Telemetry topic
  char topic_attributes[128];         // Attributes topic
  char topic_control[128];            // Control topic
  char topic_status[128];             // Status topic
  uint8_t qos;                        // Quality of Service
  uint16_t publish_interval;          // Publish interval
  uint16_t connection_timeout;        // Connection timeout
  uint16_t reconnect_delay_ms;        // Reconnect delay in ms
  uint16_t loop_interval_ms;          // Loop interval in ms
  uint16_t payload_buffer_size;       // Payload buffer size
  uint16_t attributes_buffer_size;    // Attributes buffer size
  mqtt_system_fields_t system_fields; // System fields config
} mqtt_config_t;

// TCP socket options
typedef struct {
  uint8_t keepalive;           // Keep-alive enabled
  uint16_t keepalive_idle;     // Keep-alive idle time
  uint16_t keepalive_interval; // Keep-alive interval
  uint8_t keepalive_count;     // Keep-alive count
  uint8_t tcp_nodelay;         // TCP no delay option
} tcp_socket_options_t;

// TCP system fields
typedef struct {
  char data_source[64];        // Data source identifier
  uint8_t include_timestamp;   // Include timestamp flag
  uint8_t include_gateway_ip;  // Include gateway IP flag
  uint8_t include_node_count;  // Include node count flag
  uint8_t include_system_info; // Include system info flag
} tcp_system_fields_t;

// TCP protocol settings
typedef struct {
  char message_delimiter[8];   // Message delimiter
  uint16_t max_message_size;   // Max message size
  uint8_t compression_enabled; // Compression enabled flag
  uint8_t encryption_enabled;  // Encryption enabled flag
} tcp_protocol_settings_t;

// TCP features
typedef struct {
  uint8_t config_download;  // Config download feature
  uint8_t control_commands; // Control commands feature
  uint8_t telemetry_upload; // Telemetry upload feature
  uint8_t status_reporting; // Status reporting feature
} tcp_features_t;

// TCP configuration
typedef struct {
  char server_host[128];                     // TCP server host
  uint16_t server_port;                      // TCP server port
  char config_server_host[128];              // Config server host
  uint16_t config_server_port;               // Config server port
  char client_id[64];                        // TCP client ID
  char protocol_version[16];                 // Protocol version
  char data_format[16];                      // Data format
  uint16_t send_interval;                    // Send interval
  uint16_t connection_timeout;               // Connection timeout
  uint16_t reconnect_delay_ms;               // Reconnect delay in ms
  uint16_t loop_interval_ms;                 // Loop interval in ms
  uint16_t payload_buffer_size;              // Payload buffer size
  uint16_t receive_buffer_size;              // Receive buffer size
  tcp_socket_options_t socket_options;       // Socket options
  tcp_system_fields_t system_fields;         // System fields
  tcp_protocol_settings_t protocol_settings; // Protocol settings
  tcp_features_t features;                   // Features config
} tcp_config_t;

// UDP socket options
typedef struct {
  uint8_t broadcast;           // Broadcast enabled
  uint8_t reuse_addr;          // Reuse address flag
  uint8_t reuse_port;          // Reuse port flag
  uint16_t receive_timeout_ms; // Receive timeout in ms
  uint16_t send_timeout_ms;    // Send timeout in ms
} udp_socket_options_t;

// UDP system fields
typedef struct {
  char data_source[64];        // Data source identifier
  uint8_t include_timestamp;   // Include timestamp flag
  uint8_t include_gateway_ip;  // Include gateway IP flag
  uint8_t include_node_count;  // Include node count flag
  uint8_t include_system_info; // Include system info flag
} udp_system_fields_t;

// UDP protocol settings
typedef struct {
  char message_delimiter[8];   // Message delimiter
  uint16_t max_message_size;   // Max message size
  uint8_t compression_enabled; // Compression enabled flag
  uint8_t encryption_enabled;  // Encryption enabled flag
  uint8_t checksum_enabled;    // Checksum enabled flag
} udp_protocol_settings_t;

// UDP features
typedef struct {
  uint8_t control_commands;    // Control commands feature
  uint8_t telemetry_upload;    // Telemetry upload feature
  uint8_t status_reporting;    // Status reporting feature
  uint8_t broadcast_discovery; // Broadcast discovery feature
  uint8_t multicast_support;   // Multicast support feature
} udp_features_t;

// UDP configuration
typedef struct {
  char server_host[128];                     // UDP server host
  uint16_t server_port;                      // UDP server port
  char client_id[64];                        // UDP client ID
  char protocol_version[16];                 // Protocol version
  char data_format[16];                      // Data format
  uint16_t send_interval;                    // Send interval
  uint16_t connection_timeout;               // Connection timeout
  uint16_t reconnect_delay_ms;               // Reconnect delay in ms
  uint16_t loop_interval_ms;                 // Loop interval in ms
  uint16_t payload_buffer_size;              // Payload buffer size
  uint16_t receive_buffer_size;              // Receive buffer size
  udp_socket_options_t socket_options;       // Socket options
  udp_system_fields_t system_fields;         // System fields
  udp_protocol_settings_t protocol_settings; // Protocol settings
  udp_features_t features;                   // Features config
} udp_config_t;

// Main gateway configuration
typedef struct {
  modules_config_t modules;      // Modules configuration
  system_config_t system_config; // System configuration
  system_info_t system_info;     // System information
  uart_config_t uart_config;     // UART configuration
  modbus_config_t modbus_config; // Modbus configuration
  can_config_t can_config;       // CAN configuration
  mqtt_config_t mqtt_config;     // MQTT configuration
  tcp_config_t tcp_config;       // TCP configuration
  udp_config_t udp_config;       // UDP configuration
} gateway_config_t;

// Thread pause control structure
typedef struct {
  bool is_paused;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
} thread_pause_t;

// Global mutex for thread safety
extern pthread_mutex_t gateway_config_mutex;

// Function declarations
int load_gateway_config(const char *config_file);
void cleanup_gateway_config(void);

// Getter functions
system_config_t *get_system_config(void);
system_info_t *get_system_info(void);
modules_config_t *get_modules_config(void);
uart_config_t *get_uart_config(void);
modbus_config_t *get_modbus_config(void);
can_config_t *get_can_config(void);
mqtt_config_t *get_mqtt_config(void);
tcp_config_t *get_tcp_config(void);
udp_config_t *get_udp_config(void);

#endif // GATEWAY_CONFIG_H
