#include "modbus_handler.h"

int modbus_fd = -1;
static pthread_mutex_t modbus_mutex = PTHREAD_MUTEX_INITIALIZER;

// Convert Modbus frame to byte buffer for sending
static void Modbus_Frame_To_Buffer(data_frame_t *frame, uint8_t *buffer, size_t buffer_size)
{
    if (!frame || !buffer || buffer_size < frame->frame_length)
    {
        return;
    }
    size_t idx = 0;
    // Address (1 byte)
    buffer[idx++] = frame->address;
    // Function code (1 byte) - extract actual value from union
    buffer[idx++] = frame->function.custom;
    // Data payload
    size_t data_len = frame->frame_length - 4; // Total - (addr + func + crc_16)
    if (data_len > 0 && frame->data != NULL)
    {
        memcpy(&buffer[idx], frame->data, data_len);
        idx += data_len;
    }
    frame->crc = Modbus_Calculate_CRC(buffer, idx);
    // CRC (2 bytes) - Little Endian
    buffer[idx++] = (uint8_t)(frame->crc & 0xFF);
    buffer[idx++] = (uint8_t)((frame->crc >> 8) & 0xFF);
}

// Initialize Modbus communication
void Modbus_Init(speed_t baudrate, char *device)
{
    if (pthread_mutex_lock(&modbus_mutex) != 0)
    {
        return;
    }

    // Close existing connection if open
    if (modbus_fd != -1)
    {
        close(modbus_fd);
    }

    modbus_fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (modbus_fd == -1)
    {
        perror("Failed to open Modbus device");
        pthread_mutex_unlock(&modbus_mutex);
        return;
    }

    struct termios options;
    if (tcgetattr(modbus_fd, &options) != 0)
    {
        perror("Failed to get Modbus UART attributes");
        close(modbus_fd);
        modbus_fd = -1;
        pthread_mutex_unlock(&modbus_mutex);
        return;
    }

    // Set baud rate, data bits, parity, stop bits, and other flags
    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);
    options.c_cflag = (options.c_cflag & ~CSIZE) | CS8; // 8 data bits
    options.c_cflag &= ~PARENB;                         // No parity
    options.c_cflag &= ~CSTOPB;                         // 1 stop bit
    options.c_cflag |= CLOCAL | CREAD;                  // Ignore modem control lines, enable receiver
    options.c_iflag = IGNPAR;                           // Ignore parity errors
    options.c_oflag = 0;                                // No output processing
    options.c_lflag = 0;                                // No input processing

    // For Modbus RTU timing requirements
    options.c_cc[VMIN] = 0;  // Non-blocking read
    options.c_cc[VTIME] = 5; // 0.5 second timeout

    tcflush(modbus_fd, TCIFLUSH); // Flush input buffer
    if (tcsetattr(modbus_fd, TCSANOW, &options) != 0)
    {
        perror("Failed to set Modbus UART attributes");
        close(modbus_fd);
        modbus_fd = -1;
        pthread_mutex_unlock(&modbus_mutex);
        return;
    }
    pthread_mutex_unlock(&modbus_mutex);
}

// Write Modbus frame
void Modbus_Write_Frame(data_frame_t *frame)
{
    if (!frame)
    {
        return;
    }
    pthread_mutex_lock(&modbus_mutex);
    // Check if Modbus is initialized
    if (modbus_fd == -1)
    {
        pthread_mutex_unlock(&modbus_mutex);
        return;
    }
    // Temporary buffer to hold the serialized frame
    uint8_t buffer[256]; // Max Modbus RTU frame size (typically 256 bytes)
    size_t buffer_size = sizeof(buffer);

    // Serialize frame to buffer
    Modbus_Frame_To_Buffer(frame, buffer, buffer_size);

    // Write entire frame to serial port
    ssize_t written = write(modbus_fd, buffer, frame->frame_length);

    if (written != (ssize_t)frame->frame_length)
    {
        // Handle partial write or error
        perror("Modbus frame write error");
    }
    else
    {
        // Force transmission of data (wait until all data is sent)
        tcdrain(modbus_fd);
    }
    pthread_mutex_unlock(&modbus_mutex);
}

// Read Modbus response and return data frame structure
data_frame_t *Modbus_Read_Response(uint32_t timeout_ms)
{
}

// Check if data is available for reading
int Modbus_Check_Data_Available(void)
{
}

// Calculate CRC for Modbus frame using CRC-16-IBM algorithm
uint16_t Modbus_Calculate_CRC(uint8_t *data, uint16_t length)
{
    if (!data || length == 0)
    {
        return 0;
    }
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i];

        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001; // Modbus CRC polynomial
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Verify CRC for Modbus frame
uint8_t Modbus_Verify_CRC(data_frame_t *frame, uint16_t frame_length)
{
}

// Build Modbus frame
void Modbus_Build_Frame(data_frame_t *frame, uint8_t address, uint8_t function_code, uint8_t *data, uint16_t data_len)
{
}

// Clean up Modbus frame
void Modbus_Free_Frame(data_frame_t *frame)
{
}