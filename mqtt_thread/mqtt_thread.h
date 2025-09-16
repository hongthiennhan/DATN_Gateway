#ifndef MQTT_THREAD_H
#define MQTT_THREAD_H

#include "main.h"
#include "gateway_config.h"
#include "thread_func.h"

extern thread_pause_t mqtt_pause;

void *mqtt_thread_func(void *arg);

// Get local IP address for gateway identification
char *mqtt_get_local_ip(void);

// Safe MQTT config access with mutex protection
mqtt_config_t *safe_get_mqtt_config(void);

#endif // MQTT_THREAD_H
