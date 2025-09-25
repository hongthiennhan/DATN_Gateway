#ifndef __MAIN_H__
#define __MAIN_H__

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <ncurses.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h> // ADDED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h> // ADDED
#include <termios.h>
#include <time.h>
#include <unistd.h>


// Include necessary lib for MQTT
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <mosquitto.h>
#include <netinet/in.h>
#include <sys/socket.h>


// NEW: Add json-c include
#include <json-c/json.h>

// ===== ADDITIONAL HEADERS FOR ROBUST FILE OPERATIONS =====
#include <errno.h>    // for errno, EEXIST
#include <fcntl.h>    // for open(), O_* flags
#include <sys/stat.h> // for stat(), mkdir()
#include <unistd.h>   // for fsync(), close()


#endif
