#ifndef TCP_THREAD_H
#define TCP_THREAD_H

#include "node_config.h"

extern thread_pause_t tcp_pause;
void *tcp_thread_func(void *arg);
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len);
char *get_local_ip(void);

#endif // TCP_THREAD_H