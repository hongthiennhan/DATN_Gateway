#ifndef THREAD_FUNC_H
#define THREAD_FUNC_H

#include "main.h"
#include "uart_handler.h"
#include "node_config.h"  // Include to get node_config_t and menu_item_t types
#include "modbus_handler.h" // Include Modbus handler for Modbus data types
#include "CAN_handler.h" // Include CAN handler for CAN data types
#include "mqtt_thread.h" // Include MQTT thread header for MQTT handling
typedef struct shared_data_s {
    void* data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

typedef struct thread_pause_s {
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
extern int shared_node_type;

// Pause control for threads
extern thread_pause_t uart_pause;
extern thread_pause_t modbus_pause;
extern thread_pause_t can_pause;

// ========== Function prototypes ==========
void *uart_thread_func(void *arg);
void *ui_thread_func(void *arg);
void *modbus_thread_func(void *arg);
void *can_thread_func(void *arg);

// Utility function
uint8_t hex_string_to_uint8(const char *hex_str);
size_t hex_string_to_bytes(const char *hex_str, uint8_t *output, size_t max_bytes);
void pause_thread(thread_pause_t *pause_ctrl);
void resume_thread(thread_pause_t *pause_ctrl);
#endif // THREAD_FUNC_H
