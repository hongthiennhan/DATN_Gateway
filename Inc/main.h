#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdlib.h>
#include <stdio.h>      // For printf and perror
#include <pthread.h>    // For mutex operations
#include <fcntl.h>      // For opening UART file descriptor
#include <termios.h>    // For UART configuration
#include <unistd.h>     // For read, write, and usleep
#include <string.h>     // For strlen (if needed)
#include <errno.h>      // For errno in error handling
#include <gpiod.h>
#include <stdint.h>
#include <getopt.h>     // For command line option parsing
#include <semaphore.h>  // For semaphores
#include <ncurses.h> // For ncurses library

#endif