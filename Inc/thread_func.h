#ifndef THREAD_FUNC_H
#define THREAD_FUNC_H

#include "main.h"
#include "uart_handler.h"
#include "node_config.h"  // Include này để có node_config_t và menu_item_t types

typedef struct shared_data_s {
    void* data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

typedef struct {
    int t1, t2, t3;
} node1_data_t;

// ========== External declarations for shared state ==========
extern volatile uint8_t is_busy;
extern pthread_mutex_t command_mutex;
extern int command_pending;
extern char status_response[100];
extern int status_color;
extern unsigned char receive_data[512];
extern unsigned char save_data[512];
extern pthread_cond_t cond;
extern shared_data_t command_data;
extern shared_data_t mqtt_data_n1;
extern shared_data_t mqtt_data_n2;
extern int shared_node_type;

// ========== Function prototypes ==========
void *uart_thread_func(void *arg);
void *ui_thread_func(void *arg);
void *mqtt_thread_func(void *arg);
void process_uart_data(unsigned char *data, uint16_t data_len);

// Config-driven function declarations - FIXED: Use actual typedefs instead of struct forward declarations
void process_uart_response(node_config_t *node, int cmd, unsigned char *resp, uint16_t resp_len, int silent);
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len);
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp);

#endif // THREAD_FUNC_H
