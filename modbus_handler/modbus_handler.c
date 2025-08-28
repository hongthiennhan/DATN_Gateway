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
data_frame_t *Modbus_Read_Response(uint32_t timeout_ms) {
#ifdef DEBUG
    printf("[DEBUG] Modbus_Read_Response: Starting with timeout %u ms\n", timeout_ms);
#endif
    if (pthread_mutex_lock(&modbus_mutex) != 0) {
#ifdef DEBUG
        printf("[ERROR] Failed to lock modbus mutex\n");
#endif
        return NULL;
    }

    if (modbus_fd == -1) {
#ifdef DEBUG
        printf("[ERROR] Modbus not initialized (fd = -1)\n");
#endif
        pthread_mutex_unlock(&modbus_mutex);
        return NULL;
    }

    // Get buffer size from config
    modbus_config_t *config = get_modbus_config();
    int buffer_size = config ? config->response_buffer_size : 512;
    uint32_t poll_interval_ms = config ? config->poll_interval_ms : 20;

    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
#ifdef DEBUG
        printf("[ERROR] Memory allocation failed\n");
#endif
        pthread_mutex_unlock(&modbus_mutex);
        return NULL;
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;

    // Buffered reading loop - accumulate data until complete frame
    while (waited_ms < timeout_ms && total_read < buffer_size) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(modbus_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = poll_interval_ms / 1000;
        timeout.tv_usec = (poll_interval_ms % 1000) * 1000;

        int ready = select(modbus_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
#ifdef DEBUG
            printf("[ERROR] select() failed: %s\n", strerror(errno));
#endif
            break;
        } else if (ready == 0) {
            waited_ms += poll_interval_ms;
            continue;
        }

        // Read available data and accumulate
        ssize_t r = read(modbus_fd, buffer + total_read, buffer_size - total_read);
        if (r > 0) {
            total_read += r;

            // Determine if we have a complete standard Modbus frame
            if (total_read >= 3) {
                uint8_t func_code = buffer[1];
                
                // Check if this looks like standard Modbus response
                if (func_code == 0x01 || func_code == 0x02 || func_code == 0x03 || func_code == 0x04) {
                    // Read functions - check if we have byte count
                    uint8_t byte_count = buffer[2];
                    size_t expected_frame_size = 3 + byte_count + 2; // addr + func + count + data + CRC
                    
                    if (total_read >= expected_frame_size) {
#ifdef DEBUG
                        printf("[DEBUG] Complete Modbus Read frame detected (%zd bytes)\n", total_read);
#endif
                        break;
                    }
                } else if (func_code == 0x05 || func_code == 0x06 || func_code == 0x0F || func_code == 0x10) {
                    // Write functions - fixed 8 bytes
                    if (total_read >= 8) {
#ifdef DEBUG
                        printf("[DEBUG] Complete Modbus Write frame detected (%zd bytes)\n", total_read);
#endif
                        break;
                    }
                }
            }

            // Safety break if frame gets too large
            if (total_read >= 32) {
#ifdef DEBUG
                printf("[DEBUG] Max frame size reached, processing...\n");
#endif
                break;
            }

        } else if (r < 0) {
#ifdef DEBUG
            printf("[ERROR] read() failed: %s\n", strerror(errno));
#endif
            break;
        }
    }

    pthread_mutex_unlock(&modbus_mutex);

    if (total_read == 0) {
#ifdef DEBUG
        printf("[ERROR] No data received within timeout\n");
#endif
        free(buffer);
        return NULL;
    }

    // Print final frame (essential for debugging)
#ifdef DEBUG
    printf("[DEBUG] Frame received (%zd bytes): ", total_read);
    for (int i = 0; i < total_read; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
#endif

    // Minimum frame validation
    if (total_read < 5) {
#ifdef DEBUG
        printf("[ERROR] Frame too short - minimum 5 bytes required\n");
#endif
        free(buffer);
        return NULL;
    }

    // Parse as Standard Modbus Frame
    uint8_t slave_addr = buffer[0];
    uint8_t func_code = buffer[1];
    
#ifdef DEBUG
    printf("[DEBUG] Parsing: Slave=0x%02X, Function=0x%02X\n", slave_addr, func_code);
#endif

    size_t frame_size = total_read;
    size_t data_start_pos = 2;
    size_t data_length = 0;

    // Determine frame structure based on function code
    if (func_code == 0x01 || func_code == 0x02 || func_code == 0x03 || func_code == 0x04) {
        // Read responses: [addr][func][byte_count][data...][CRC_high][CRC_low]
        if (total_read >= 3) {
            uint8_t byte_count = buffer[2];
            frame_size = 3 + byte_count + 2; // addr + func + count + data + CRC
            data_start_pos = 3; // Data starts after byte count
            data_length = byte_count;
            
            if (frame_size > total_read) {
                frame_size = total_read; // Use what we have
            }
        }
    } else if (func_code == 0x05 || func_code == 0x06 || func_code == 0x0F || func_code == 0x10) {
        // Write responses: [addr][func][data_addr_high][data_addr_low][data_high][data_low][CRC_high][CRC_low]
        frame_size = 8; // Fixed size
        data_start_pos = 2;
        data_length = 4; // 4 bytes of response data
        
        if (frame_size > total_read) {
            frame_size = total_read;
        }
    }

    // Extract and verify CRC (BIG ENDIAN)
    if (frame_size >= 4) {
        uint8_t crc_high = buffer[frame_size - 2];  
        uint8_t crc_low = buffer[frame_size - 1];   
        uint16_t received_crc = (crc_high << 8) | crc_low;  // BIG ENDIAN
        
        // Calculate CRC on all data except CRC bytes
        uint16_t calculated_crc = Modbus_Calculate_CRC(buffer, frame_size - 2);

#ifdef DEBUG
        printf("[DEBUG] CRC Check: Calculated=0x%04X, Received=0x%04X\n", calculated_crc, received_crc);
#endif

        if (calculated_crc != received_crc) {
#ifdef DEBUG
            printf("[ERROR] CRC verification failed (Expected: 0x%04X, Got: 0x%04X)\n", 
                   calculated_crc, received_crc);
#endif
            free(buffer);
            return NULL;
        }
#ifdef DEBUG
        printf("[DEBUG] CRC verification PASSED\n");
#endif
    }

    // Create and fill data frame structure
    data_frame_t *frame = malloc(sizeof(data_frame_t));
    if (!frame) {
#ifdef DEBUG
        printf("[ERROR] Memory allocation failed for frame structure\n");
#endif
        free(buffer);
        return NULL;
    }

    // Fill frame structure with parsed Modbus data
    frame->address = slave_addr;
    frame->function.custom = func_code;
    frame->frame_length = frame_size;
    frame->crc = (buffer[frame_size - 2] << 8) | buffer[frame_size - 1]; // Big Endian CRC

    // Extract data payload
    if (data_length > 0 && data_start_pos + data_length <= frame_size - 2) {
        frame->data = malloc(data_length);
        if (frame->data) {
            memcpy(frame->data, &buffer[data_start_pos], data_length);
            
            // Print data payload (essential for debugging)
#ifdef DEBUG
            printf("[DEBUG] Data payload (%zu bytes): ", data_length);
            for (size_t i = 0; i < data_length; i++) {
                printf("%02X ", ((uint8_t*)frame->data)[i]);
            }
            printf("\n");
#endif
        } else {
#ifdef DEBUG
            printf("[ERROR] Memory allocation failed for data payload\n");
#endif
            free(frame);
            free(buffer);
            return NULL;
        }
    } else {
        frame->data = NULL;
    }

#ifdef DEBUG
    printf("[DEBUG] Successfully parsed Modbus frame\n");
#endif
    free(buffer);
    return frame;
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