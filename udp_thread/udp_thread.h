#ifndef UDP_THREAD_H
#define UDP_THREAD_H

#include "node_config.h"

extern thread_pause_t udp_pause;
void *udp_thread_func(void *arg);

#endif // UDP_THREAD_H
