#ifndef THREAD_FUNC_H
#define THREAD_FUNC_H

#include "main.h"
#include "uart_handler.h"
#include "gateway_config.h" // Include to get node_config_t and menu_item_t types
#include "modbus_handler.h" // Include Modbus handler for Modbus data types
#include "CAN_handler.h"    // Include CAN handler for CAN data types
#include "mqtt_thread.h"    // Include MQTT thread header for MQTT handling
#include "tcp_thread.h"     // Include TCP thread header for TCP handling
#include "udp_thread.h"     // Include UDP thread header for UDP handling
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
extern thread_pause_t uart_pause;
extern thread_pause_t modbus_pause;
extern thread_pause_t can_pause;
// Utility function
uint8_t hex_string_to_uint8(const char *hex_str);
size_t hex_string_to_bytes(const char *hex_str, uint8_t *output, size_t max_bytes);
void pause_thread(thread_pause_t *pause_ctrl);
void resume_thread(thread_pause_t *pause_ctrl);
#endif // THREAD_FUNC_H
