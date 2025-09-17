#ifndef UDP_THREAD_H
#define UDP_THREAD_H

#include "main.h"
#include "gateway_config.h"
#include "thread_func.h"
#include "mongoose.h"

extern thread_pause_t udp_pause;

// Main UDP thread function
void *udp_thread_func(void *arg);

// Get local IP address for gateway identification
char *udp_get_local_ip(void);

// Update received data from external tasks - can be called from other threads
void update_udp_received_data(const unsigned char *data, uint16_t data_len);

// Clear stored received data
void clear_udp_received_data(void);

#endif // UDP_THREAD_H
