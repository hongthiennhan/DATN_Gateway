#ifndef MQTT_THREAD_H
#define MQTT_THREAD_H
#include "main.h"
#include "node_config.h"  

extern thread_pause_t mqtt_pause;
void *mqtt_thread_func(void *arg);

// Config-driven function declarations - FIXED: Use actual typedefs instead of struct forward declarations
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len);
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp);
int request_config_json_robust(void);
char *get_local_ip(void);
mqtt_config_t *safe_get_mqtt_config(void);
#endif // MQTT_THREAD_H