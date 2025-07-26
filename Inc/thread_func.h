#ifndef THREAD_FUNC_H
#define THREAD_FUNC_H

#include "main.h"
#include "control_command.h"

// ========== Node Types ==========
typedef enum {
    NODE_TYPE_1 = 1,
    NODE_TYPE_2
} NodeType;

typedef struct {
    void* data;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} shared_data_t;

// ========== External declarations for shared state ==========
extern volatile uint8_t is_busy;

extern pthread_mutex_t command_mutex;
extern int command_pending;
extern char status_response[100];
extern int status_color;
extern unsigned char receive_data[512];
extern unsigned char save_data[512];
extern pthread_cond_t cond;
extern  shared_data_t command_data;
extern shared_data_t mqtt_data_n1;
extern shared_data_t mqtt_data_n2;
// ========== Function prototypes ==========
void *uart_thread_func(void *arg);
void *ui_thread_func(void *arg);
void *mqtt_thread_func(void *arg);

// ========== Helper Functions ==========
void set_command_code(int new_code);
int get_command_code();
int wait_for_command_change(int timeout_ms);
void set_mqtt_data_n1(int t1, int t2, int t3);
node1_data_t get_mqtt_data_n1();
void set_mqtt_data_n2(uint16_t adc_value);
uint16_t get_mqtt_data_n2();
int wait_for_mqtt_data_by_node(int node_type, int timeout_ms);

// ========== Macros for exit codes ==========
#define NODE1_EXIT_CODE 10
#define NODE2_EXIT_CODE 5

#endif // THREAD_FUNC_H