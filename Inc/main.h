#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <semaphore.h>
#include <ncurses.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>      // For printf and perror
#include <pthread.h>    // For mutex operations
#include <fcntl.h>      // For opening UART file descriptor
#include <termios.h>    // For UART configuration
#include <unistd.h>     // For read, write, and usleep
#include <string.h>     // For strlen (if needed)
#include <errno.h>      // For errno in error handling
#include <stdint.h>
#include <getopt.h>     // For command line option parsing
#include <semaphore.h>  // For semaphores
#include <ncurses.h> // For ncurses library
#include <sys/time.h>

// Include necessary lib for MQTT
#include <mosquitto.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

// NEW: Add json-c include
#include <json-c/json.h>

// ===== ADDITIONAL HEADERS FOR ROBUST FILE OPERATIONS =====
#include <sys/stat.h>     // for stat(), mkdir()
#include <fcntl.h>        // for open(), O_* flags  
#include <errno.h>        // for errno, EEXIST
#include <unistd.h>       // for fsync(), close()


#endif
