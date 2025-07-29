#ifndef __CONTROL_COMMAND_H__
#define __CONTROL_COMMAND_H__

#include "main.h"

#define CONFIG_FILE "/tmp/uart_config.txt" 

typedef enum {
    CMD_DIRECTION_1  = 0xA1,
    CMD_DIRECTION_2  = 0xA2,
    CMD_DIRECTION_3  = 0xA3,
    CMD_SYS_ON       = 0xA4,
    CMD_SYS_OFF      = 0xA5,
    CMD_SEND_STATUS  = 0xA6,
    CMD_STOP_SYSTEM  = 0xA7,
    CMD_INIT         = 0xFF
} Command;

typedef enum {
    CMD2_LED_ON          = 0xA1,
    CMD2_LED_OFF         = 0xA2,
    CMD2_READ_SINGLE     = 0xA3
} Command_2;

extern int uart_fd;  
extern sem_t *uart_sem;
void Uart_Init(speed_t baudrate, char *device);
void write_command(uint8_t cmd);
void write_init(uint32_t baudrate);
unsigned char* Read_Response(uint32_t timeout_ms, uint16_t* bytes_read_out);
speed_t map_to_speed(uint32_t baud_num);
void save_config(uint32_t baud, const char *dev);
uint8_t load_config(uint32_t *baud, char **dev);
void Clear_Startup_UART(int fd, uint32_t flush_duration_ms);

#endif // __CONTROL_COMMAND_H__