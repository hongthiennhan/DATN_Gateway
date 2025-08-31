#ifndef THREAD_FUNC_H
#define THREAD_FUNC_H

#include "main.h"
#include "uart_handler.h"
#include "node_config.h"  // Include này để có node_config_t và menu_item_t types
#include "modbus_handler.h" // Include Modbus handler for Modbus data types
#include "can_handler.h" // Include CAN handler for CAN data types

typedef struct shared_data_s {
    void* data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

typedef struct {
    int t1, t2, t3;
} node1_data_t;

typedef struct {
    bool is_paused;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} thread_pause_t;

// ========== External declarations for shared state ==========
extern volatile uint8_t is_busy;
extern pthread_mutex_t command_mutex;
extern int command_pending;
extern char status_response[256];
extern int status_color;
extern unsigned char receive_data[512];
extern unsigned char send_data[512];
extern pthread_cond_t cond;
extern shared_data_t command_data;
extern shared_data_t mqtt_data_n1;
extern shared_data_t mqtt_data_n2;
extern int shared_node_type;

// Pause control for threads
extern thread_pause_t uart_pause;
extern thread_pause_t modbus_pause;
extern thread_pause_t mqtt_pause;

// ========== Function prototypes ==========
void *uart_thread_func(void *arg);
void *ui_thread_func(void *arg);
void *modbus_thread_func(void *arg);
void *can_thread_func(void *arg);
void *mqtt_thread_func(void *arg);

// Config-driven function declarations - FIXED: Use actual typedefs instead of struct forward declarations
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len);
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp);

// Utility function
uint8_t hex_string_to_uint8(const char *hex_str);
size_t hex_string_to_bytes(const char *hex_str, uint8_t *output, size_t max_bytes);
void pause_thread(thread_pause_t *pause_ctrl);
void resume_thread(thread_pause_t *pause_ctrl);
#endif // THREAD_FUNC_H
