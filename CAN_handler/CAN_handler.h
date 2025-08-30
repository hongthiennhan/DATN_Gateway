#ifndef __CAN_HANDLER_H__
#define __CAN_HANDLER_H__
#include "main.h"

#define CAN_USB_HEADER 0xAA
#define CAN_USB_COMMAND 0xC2
#define CAN_USB_FOOTER 0x55

#define CAN_STM_HEADER 0xA0

typedef struct {
    uint8_t header;
    uint16_t ID; // 11 bit identifier
    uint8_t RTR; // 1 bit Remote Transmission Request
    uint8_t IDE; // 1 bit Identifier Extension
    uint8_t reserved; // 1 bits reserved
    uint8_t DLC; // 4 bits Data Length Code
    uint8_t* data; // 0 - 8 bytes of data
    uint8_t data_len;
} CAN_Message;

typedef struct{
    uint8_t header;
    uint8_t command;
    uint16_t id;
    uint8_t *data;
    uint8_t footer;
    uint8_t data_len;
} CAN_Message_USB;

extern int can_fd;

void CAN_Init(speed_t baudrate, char *device);
void CAN_Write_Data(uint8_t *data, size_t len);
unsigned char* CAN_Read_Response(uint32_t timeout_ms, uint16_t* bytes_read_out);
extern int Check_CAN_Data_Available(void);
void CAN_USB_to_Byte(CAN_Message_USB *usb_msg, uint8_t *byte_array);
void CAN_to_Byte(CAN_Message *can_msg, uint8_t *byte_array);

#endif