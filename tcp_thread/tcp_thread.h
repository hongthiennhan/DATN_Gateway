#ifndef TCP_THREAD_H
#define TCP_THREAD_H

#include "main.h"
#include "gateway_config.h"
#include "thread_func.h"
#include "mongoose.h"

extern thread_pause_t tcp_pause;

// Main TCP thread function
void *tcp_thread_func(void *arg);

// Get local IP address for gateway identification
char *tcp_get_local_ip(void);

// Update received data from external tasks - can be called from other threads
void tcp_update_received_data(const unsigned char *data, uint16_t data_len);

// Clear stored received data
void tcp_clear_received_data(void);

#endif // TCP_THREAD_H
