#ifndef MQTT_THREAD_H
#define MQTT_THREAD_H

#include "main.h"
#include "gateway_config.h"
#include "thread_func.h"
#include "mongoose.h"

extern thread_pause_t mqtt_pause;

// Main MQTT thread function
void *mqtt_thread_func(void *arg);

// Get local IP address for gateway identification
char *mqtt_get_local_ip(void);

// Update received data from external tasks - can be called from other threads
void mqtt_update_received_data(const unsigned char *data, uint16_t data_len);

// Clear stored received data
void mqtt_clear_received_data(void);

#endif // MQTT_THREAD_H
