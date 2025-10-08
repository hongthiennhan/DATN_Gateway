#ifndef THREAD_FUNC_H
#define THREAD_FUNC_H

#include "gateway_config.h" // Include to get node_config_t and menu_item_t types
#include "main.h"
#include "mqtt_thread.h" // Include MQTT thread header for MQTT handling
#include "tcp_thread.h"  // Include TCP thread header for TCP handling
#include "uart_handler.h"
#include "udp_thread.h" // Include UDP thread header for UDP handling

// ========== External declarations for shared state ==========
extern volatile uint8_t is_busy;
extern pthread_mutex_t command_mutex;
extern char status_response[256];
extern uint32_t status_color;
extern unsigned char uplink_data[512];
extern pthread_mutex_t uplink_mutex;
extern uint8_t check_uplink;
extern unsigned char downlink_data[512];
extern pthread_mutex_t downlink_mutex;
extern uint8_t check_downlink;
extern pthread_cond_t cond;

// Pause control for threads
extern thread_pause_t config_thread_pause;
extern thread_pause_t data_thread_pause;
extern volatile uint8_t config_updated;
extern pthread_mutex_t config_update_mutex;

extern void *data_thread_func(void *arg);
extern void *config_thread_func(void *arg);
// Utility functions
void pause_thread(thread_pause_t *pause_ctrl);
void resume_thread(thread_pause_t *pause_ctrl);
#endif // THREAD_FUNC_H
