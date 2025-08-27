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
    buffer[idx++] = (uint8_t)((frame->crc >> 8) & 0xFF);
    buffer[idx++] = (uint8_t)(frame->crc & 0xFF);
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
    uint8_t buffer[256] = {0}; // Max Modbus RTU frame size (typically 256 bytes)
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
// Check if data is available for reading
int Modbus_Check_Data_Available(void) {
    if (pthread_mutex_lock(&modbus_mutex) != 0) {
        return 0; // Failed to lock mutex
    }

    if (modbus_fd == -1) {
        pthread_mutex_unlock(&modbus_mutex);
        return 0; // Modbus not initialized
    }

    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(modbus_fd, &readfds);

    // Short timeout (10 ms) for non-blocking check
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    int ret = select(modbus_fd + 1, &readfds, NULL, NULL, &timeout);

    pthread_mutex_unlock(&modbus_mutex);

    if (ret > 0 && FD_ISSET(modbus_fd, &readfds)) {
        return 1; // Data available
    }

    return 0; // No data available
}

// Read Modbus response and return data frame structure
data_frame_t *Modbus_Read_Response(uint32_t timeout_ms) {
    if (pthread_mutex_lock(&modbus_mutex) != 0) {
        return NULL; // Failed to lock mutex
    }

    if (modbus_fd == -1) {
        pthread_mutex_unlock(&modbus_mutex);
        return NULL; // Modbus not initialized
    }

    // Get buffer size from config, fallback to safe default
    modbus_config_t *config = get_modbus_config();
    int buffer_size = config ? config->response_buffer_size : 512;
    uint32_t poll_interval_ms = config ? config->poll_interval_ms : 20;

    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        pthread_mutex_unlock(&modbus_mutex);
        return NULL; // Memory allocation failed
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;

    // Read response with timeout using polling approach
    while (waited_ms < timeout_ms && total_read < buffer_size) {
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(modbus_fd, &readfds);

        tv.tv_sec = poll_interval_ms / 1000;
        tv.tv_usec = (poll_interval_ms % 1000) * 1000;

        int ret = select(modbus_fd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret < 0) {
            perror("select() failed in Modbus_Read_Response");
            break;
        } else if (ret == 0) {
            waited_ms += poll_interval_ms; // Increment wait time
            continue;
        }

        // Data available - read from Modbus port
        ssize_t bytes_read = read(modbus_fd, buffer + total_read, buffer_size - total_read);
        
        if (bytes_read > 0) {
            total_read += bytes_read;
            break; // Got data, process the frame
        } else if (bytes_read < 0) {
            perror("read() failed in Modbus_Read_Response");
            break;
        }
    }

    pthread_mutex_unlock(&modbus_mutex);

    // Check minimum frame length (address + function + CRC = 4 bytes)
    if (total_read < 4) {
        free(buffer);
        return NULL; // Invalid or incomplete frame
    }

    // Verify CRC before parsing frame
    if (!Modbus_Verify_CRC(buffer, total_read)) {
        printf("Modbus CRC verification failed\n");
        free(buffer);
        return NULL; // CRC verification failed
    }

    // Allocate memory for data frame structure
    data_frame_t *frame = malloc(sizeof(data_frame_t));
    if (!frame) {
        free(buffer);
        return NULL; // Memory allocation failed
    }

    // Parse Modbus frame components
    frame->address = buffer[0];                    // Modbus device address
    frame->function.custom = buffer[1];            // Function code
    frame->frame_length = total_read;              // Total frame length
    
    // Extract CRC (last 2 bytes, little-endian format)
    frame->crc = (uint16_t)(buffer[total_read - 2] | (buffer[total_read - 1] << 8));

    // Extract data payload (exclude address, function code, and CRC)
    size_t data_len = total_read - 4;
    if (data_len > 0) {
        frame->data = malloc(data_len);
        if (!frame->data) {
            free(frame);
            free(buffer);
            return NULL; // Memory allocation failed
        }
        memcpy(frame->data, &buffer[2], data_len); // Copy data portion
    } else {
        frame->data = NULL; // No data payload
    }

    free(buffer);
    return frame; // Successfully parsed Modbus response
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
uint8_t Modbus_Verify_CRC(uint8_t* data, uint16_t length)
{
    pthread_mutex_lock(&modbus_mutex);
    if (!data || length < 4) // At least address, function, 1 byte data and CRC
    {
        pthread_mutex_unlock(&modbus_mutex);
        return 0; // Invalid frame
    }
    uint16_t crc = Modbus_Calculate_CRC(data, length - 2);
    uint16_t received_crc = ((data[length - 2] << 8) | data[length - 1]);
    pthread_mutex_unlock(&modbus_mutex);
    return (crc == received_crc);
}

// Build Modbus frame
void Modbus_Build_Frame(data_frame_t *frame, uint8_t address, uint8_t function_code, uint8_t *data, uint16_t data_len)
{
    pthread_mutex_lock(&modbus_mutex);
    if (!frame || data_len > 252) // Max data length for Modbus RTU
    {
        pthread_mutex_unlock(&modbus_mutex);
        return;
    }
    frame->address = address;
    frame->function.custom = function_code; // Set function code
    frame->data = data; // Pointer to data buffer
    frame->frame_length = 4 + data_len; // Address + Function + Data + CRC (2 bytes)
    frame->crc = 0; // Initialize CRC
    pthread_mutex_unlock(&modbus_mutex);
}