#ifndef __CONTROL_COMMAND_H__
#define __CONTROL_COMMAND_H__

#include "main.h"

#define CONFIG_FILE "/tmp/uart_config.txt" 

typedef enum {
    CMD_DIRECTION_1  = 0xA1,
    CMD_DIRECTION_2  = 0xA2,
    CMD_DIRECTION_3  = 0xA3,
    CMD_RELAY_ON     = 0xA4,
    CMD_RELAY_OFF    = 0xA5,
    CMD_LED_ON       = 0xA6,
    CMD_LED_OFF      = 0xA7,
    CMD_SEND_STATUS  = 0xA8,
    CMD_INIT         = 0xFF
} Command;

extern const struct option long_options[];
extern int uart_fd;  

void Uart_Init(speed_t baudrate, char *device);
void write_command(Command cmd);
void write_init(uint32_t baudrate);
uint16_t Read_Response(void);
speed_t map_to_speed(uint32_t baud_num);
void save_config(uint32_t baud, const char *dev);
uint8_t load_config(uint32_t *baud, char **dev);

#endif // __CONTROL_COMMAND_H__