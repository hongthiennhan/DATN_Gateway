#ifndef TCP_THREAD_H
#define TCP_THREAD_H

#include "node_config.h"

extern thread_pause_t tcp_pause;
void *tcp_thread_func(void *arg);

#endif // TCP_THREAD_H