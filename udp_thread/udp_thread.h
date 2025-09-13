#ifndef UDP_THREAD_H
#define UDP_THREAD_H

#include "node_config.h"

extern thread_pause_t udp_pause;
void *udp_thread_func(void *arg);
void update_udp_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len);
char *udp_get_local_ip(void);
#endif // UDP_THREAD_H
