#ifndef __UART_HANDLER_H__
#define __UART_HANDLER_H__

#include "gateway_config.h"
#include "main.h"

// Remove hard-coded CONFIG_FILE - now get from config

extern int uart_fd;

void Uart_Init(speed_t baudrate, char *device);
void UART_Write_Data(uint8_t *data, size_t len);
unsigned char *UART_Read_Response(uint32_t timeout_ms,
                                  uint16_t *bytes_read_out);
speed_t UART_map_to_speed(uint32_t baud_num);
void Clear_Startup_UART(int fd, uint32_t flush_duration_ms);
extern int Check_UART_Data_Available(void);
#endif // __UART_HANDLER_H__
