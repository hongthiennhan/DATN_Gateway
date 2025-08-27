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
    printf("[DEBUG] Modbus_Read_Response: Starting with timeout %u ms\n", timeout_ms);
    
    if (pthread_mutex_lock(&modbus_mutex) != 0) {
        printf("[ERROR] Failed to lock modbus mutex\n");
        return NULL;
    }

    if (modbus_fd == -1) {
        printf("[ERROR] Modbus not initialized (fd = -1)\n");
        pthread_mutex_unlock(&modbus_mutex);
        return NULL;
    }

    // Get buffer size from config
    modbus_config_t *config = get_modbus_config();
    int buffer_size = config ? config->response_buffer_size : 512;
    uint32_t poll_interval_ms = config ? config->poll_interval_ms : 20;

    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        printf("[ERROR] Memory allocation failed\n");
        pthread_mutex_unlock(&modbus_mutex);
        return NULL;
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;
    uint8_t expected_frame_size = 0;
    uint8_t min_bytes_needed = 2; // Address + Length minimum

    printf("[DEBUG] Starting buffered read loop...\n");

    // ✅ Buffered reading loop - accumulate data until complete frame
    while (waited_ms < timeout_ms && total_read < buffer_size) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(modbus_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = poll_interval_ms / 1000;
        timeout.tv_usec = (poll_interval_ms % 1000) * 1000;

        int ready = select(modbus_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            printf("[ERROR] select() failed: %s\n", strerror(errno));
            break;
        } else if (ready == 0) {
            waited_ms += poll_interval_ms;
            continue;
        }

        // ✅ Read available data and accumulate
        ssize_t r = read(modbus_fd, buffer + total_read, buffer_size - total_read);
        if (r > 0) {
            printf("[DEBUG] Read %zd bytes, total now: %zd\n", r, total_read + r);
            
            // Print newly received bytes
            printf("[DEBUG] New bytes: ");
            for (int i = 0; i < r; i++) {
                printf("%02X ", buffer[total_read + i]);
            }
            printf("\n");
            
            total_read += r;

            // ✅ Determine expected frame size once we have length byte
            if (total_read >= 2 && expected_frame_size == 0) {
                uint8_t frame_length_field = buffer[1];
                
                // ✅ Interpret length field properly based on your protocol
                // Option 1: Length is payload size (excluding header + CRC)
                expected_frame_size = 2 + frame_length_field + 2; // Addr + Len + Payload + CRC(2)
                
                // Option 2: Length is total frame size
                // expected_frame_size = frame_length_field;
                
                printf("[DEBUG] Length field: %u, Expected total frame size: %u\n", 
                       frame_length_field, expected_frame_size);
            }

            // ✅ Check if we have complete frame
            if (expected_frame_size > 0 && total_read >= expected_frame_size) {
                printf("[DEBUG] Complete frame detected (%zd >= %u bytes)\n", 
                       total_read, expected_frame_size);
                break; // Complete frame received
            }

            // ✅ Also break if we've been accumulating for too long
            if (total_read >= 32) { // Reasonable max frame size
                printf("[DEBUG] Max frame size reached, processing...\n");
                break;
            }

        } else if (r < 0) {
            printf("[ERROR] read() failed: %s\n", strerror(errno));
            break;
        }
        // Continue accumulating if r == 0
    }

    pthread_mutex_unlock(&modbus_mutex);

    if (total_read == 0) {
        printf("[ERROR] No data received within timeout\n");
        free(buffer);
        return NULL;
    }

    printf("[DEBUG] Final accumulated frame (%zd bytes): ", total_read);
    for (int i = 0; i < total_read; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");

    // ✅ Use accumulated data for frame parsing
    if (total_read < 4) {
        printf("[ERROR] Frame too short - minimum 4 bytes required\n");
        free(buffer);
        return NULL;
    }

    // ✅ Parse frame using actual accumulated size
    uint8_t frame_address = buffer[0];
    uint8_t frame_length_field = buffer[1];
    size_t actual_frame_size = (expected_frame_size > 0) ? expected_frame_size : total_read;

    printf("[DEBUG] Frame parsing:\n");
    printf("[DEBUG]   Address: 0x%02X (%u)\n", frame_address, frame_address);
    printf("[DEBUG]   Length field: %u\n", frame_length_field);
    printf("[DEBUG]   Actual frame size: %zu bytes\n", actual_frame_size);

    // ✅ Extract and verify CRC
    if (actual_frame_size >= 4) {
        uint16_t calculated_crc = Modbus_Calculate_CRC(buffer, actual_frame_size - 2);
        uint16_t received_crc = ((buffer[actual_frame_size - 2] << 8) | buffer[actual_frame_size - 1]);

        printf("[DEBUG] CRC: Calculated=0x%04X, Received=0x%04X\n", calculated_crc, received_crc);

        if (calculated_crc != received_crc) {
            printf("[ERROR] CRC verification failed\n");
            free(buffer);
            return NULL;
        }
        printf("[DEBUG] CRC verification passed\n");
    }

    // ✅ Create and fill data frame
    data_frame_t *frame = malloc(sizeof(data_frame_t));
    if (!frame) {
        printf("[ERROR] Memory allocation failed for frame\n");
        free(buffer);
        return NULL;
    }

    frame->address = frame_address;
    frame->function.custom = frame_length_field; // Store length field here
    frame->frame_length = actual_frame_size;
    frame->crc = ((buffer[actual_frame_size - 2] << 8) | buffer[actual_frame_size - 1]);

    // Extract payload data
    size_t payload_size = actual_frame_size - 4; // Exclude addr + length + CRC
    if (payload_size > 0) {
        frame->data = malloc(payload_size);
        if (frame->data) {
            memcpy(frame->data, &buffer[2], payload_size);
        }
    } else {
        frame->data = NULL;
    }

    printf("[DEBUG] Successfully parsed complete frame\n");
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