#ifndef __MODBUS_HANDLER_H__
#define __MODBUS_HANDLER_H__

#include "main.h"
#include "node_config.h"
// Code for Modbus communication
#define CUSTOM_FUNCTION_START_0 65
#define CUSTOM_FUNCTION_END_0   72
#define CUSTOM_FUNCTION_START_1 100
#define CUSTOM_FUNCTION_END_1   110

//most used public function codes:

// Read operation function code:
// Read operation function code:
typedef enum {
    READ_COILS = 0x01,              // Read Coils (1-bit read/write)
    READ_DISCRETE_INPUTS = 0x02,    // Read Discrete Inputs (1-bit read-only)
    READ_HOLDING_REGISTERS = 0x03,  // Read Holding Registers (16-bit read/write)
    READ_INPUT_REGISTERS = 0x04     // Read Input Registers (16-bit read-only)
} read_code_t;

// Write operation function code:
typedef enum {
    WRITE_SINGLE_COIL = 0x05,               // Write Single Coil
    WRITE_SINGLE_REGISTER = 0x06,           // Write Single Register
    WRITE_MULTIPLE_COILS = 0x0F,            // Write Multiple Coils
    WRITE_MULTIPLE_REGISTERS = 0x10,        // Write Multiple Registers
    MASK_WRITE_REGISTER = 0x16,             // Mask Write Register
    READ_WRITE_MULTIPLE_REGISTERS = 0x17    // Read/Write Multiple Registers
} write_code_t;

// Diagnostic & others operation function code:
typedef enum {
    READ_EXCEPTION_STATUS = 0x07,           // Read Exception Status
    DIAGNOSTICS = 0x08,                     // Diagnostics (with subcodes)
    GET_COMM_EVENT_COUNTER = 0x0B,          // Get Comm Event Counter
    GET_COMM_EVENT_LOG = 0x0C,              // Get Comm Event Log
    REPORT_SERVER_ID = 0x11,                // Report Server ID
    READ_FILE_RECORD = 0x14,                // Read File Record
    WRITE_FILE_RECORD = 0x15,               // Write File Record
    READ_FIFO_QUEUE = 0x18,                 // Read FIFO Queue
    READ_DEVICE_IDENTIFICATION = 0x2B       // Read Device Identification (with subcode 0x0E)
} diagnostic_code_t;

typedef union {
    read_code_t read;
    write_code_t write;
    diagnostic_code_t diagnostic;
    uint8_t custom;                         // For user-defined codes (65-72, 100-110) or reserved
} function_code_t;


typedef struct{
    uint8_t address; // Modbus address
    function_code_t function;
    uint8_t *data;
    uint16_t crc;
    uint8_t frame_length;
}data_frame_t;

void Modbus_Init(void);
void Modbus_Send_Request(function_type_t function, uint8_t *data, size_t len);
uint8_t* Modbus_Receive_Response(uint32_t timeout_ms, size_t *response_len);

#endif // __MODBUS_HANDLER_H__
