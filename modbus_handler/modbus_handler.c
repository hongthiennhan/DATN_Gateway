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

    printf("[DEBUG] Modbus fd: %d\n", modbus_fd);

    // Get buffer size from config, fallback to safe default
    modbus_config_t *config = get_modbus_config();
    int buffer_size = config ? config->response_buffer_size : 512;
    uint32_t poll_interval_ms = config ? config->poll_interval_ms : 20;

    printf("[DEBUG] Buffer size: %d, Poll interval: %u ms\n", buffer_size, poll_interval_ms);

    // Allocate buffer for response
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        printf("[ERROR] Memory allocation failed for buffer size %d\n", buffer_size);
        pthread_mutex_unlock(&modbus_mutex);
        return NULL;
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;

    printf("[DEBUG] Starting read loop...\n");

    // ✅ Read loop exactly like UART_Read_Response
    while (waited_ms < timeout_ms && total_read < buffer_size) {
        // Wait for data to become available
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(modbus_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = poll_interval_ms / 1000;
        timeout.tv_usec = (poll_interval_ms % 1000) * 1000;

        printf("[DEBUG] Calling select() - waited: %u ms, timeout remaining: %u ms\n", 
               waited_ms, timeout_ms - waited_ms);

        int ready = select(modbus_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            printf("[ERROR] select() failed: %s\n", strerror(errno));
            perror("select() failed");
            break;
        } else if (ready == 0) {
            // No data yet; increment wait timer
            printf("[DEBUG] select() timeout, no data available\n");
            waited_ms += poll_interval_ms;
            continue;
        }

        printf("[DEBUG] Data available on fd %d\n", modbus_fd);

        // ✅ Data available — read from Modbus port (exactly like UART)
        ssize_t r = read(modbus_fd, buffer + total_read, buffer_size - total_read);
        if (r > 0) {
            printf("[DEBUG] Read %zd bytes from Modbus port\n", r);
            
            // Print raw bytes received
            printf("[DEBUG] Raw data received: ");
            for (int i = 0; i < r; i++) {
                printf("%02X ", buffer[total_read + i]);
            }
            printf("\n");
            
            total_read += r;
            printf("[DEBUG] Total bytes read: %zd\n", total_read);
            break; // ✅ Exit after reading data (like UART_Read_Response)
        } else if (r < 0) {
            printf("[ERROR] read() failed: %s\n", strerror(errno));
            perror("read() failed");
            break;
        }
        // Else (r == 0): continue polling
    }

    pthread_mutex_unlock(&modbus_mutex);

    printf("[DEBUG] Read loop completed. Total read: %zd, Waited: %u ms\n", total_read, waited_ms);

    if (total_read == 0) {
        // No data received
        printf("[ERROR] No data received within timeout\n");
        free(buffer);
        return NULL;
    }

    printf("[DEBUG] Complete frame received (%zd bytes): ", total_read);
    for (int i = 0; i < total_read; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");

    // ✅ Parse custom frame format: [Address][Length][Data...][CRC_High][CRC_Low]
    if (total_read < 4) {
        printf("[ERROR] Frame too short - minimum 4 bytes required (Address + Length + CRC)\n");
        free(buffer);
        return NULL;
    }

    uint8_t frame_address = buffer[0];        // Byte 1: Frame address
    uint8_t frame_length = buffer[1];         // Byte 2: Frame length
    
    printf("[DEBUG] Frame parsing:\n");
    printf("[DEBUG]   Address: 0x%02X (%u)\n", frame_address, frame_address);
    printf("[DEBUG]   Length: %u bytes\n", frame_length);

    // Validate frame length
    if (frame_length != total_read) {
        printf("[ERROR] Frame length mismatch - expected %u, got %zd\n", frame_length, total_read);
        free(buffer);
        return NULL;
    }

    if (frame_length < 4) {
        printf("[ERROR] Invalid frame length %u (minimum 4)\n", frame_length);
        free(buffer);
        return NULL;
    }

    // ✅ Extract CRC (last 2 bytes) - Big Endian format
    uint8_t crc_high = buffer[frame_length - 2];  // Second to last byte
    uint8_t crc_low = buffer[frame_length - 1];   // Last byte
    uint16_t received_crc = (crc_high << 8) | crc_low;  // Big Endian

    printf("[DEBUG]   CRC bytes: [%02X %02X]\n", crc_high, crc_low);
    printf("[DEBUG]   Received CRC (Big Endian): 0x%04X\n", received_crc);

    // ✅ Calculate CRC for data (exclude CRC bytes)
    uint16_t calculated_crc = Modbus_Calculate_CRC(buffer, frame_length - 2);

    printf("[DEBUG] CRC Check:\n");
    printf("[DEBUG]   Data for CRC (%u bytes): ", frame_length - 2);
    for (int i = 0; i < frame_length - 2; i++) {
        printf("%02X ", buffer[i]);
    }
    printf("\n");
    printf("[DEBUG]   Calculated CRC: 0x%04X\n", calculated_crc);
    printf("[DEBUG]   Received CRC: 0x%04X\n", received_crc);
    printf("[DEBUG]   CRC Match: %s\n", (calculated_crc == received_crc) ? "YES" : "NO");

    // ✅ Verify CRC
    if (calculated_crc != received_crc) {
        printf("[ERROR] CRC verification failed\n");
        printf("[ERROR]   Expected: 0x%04X, Got: 0x%04X\n", calculated_crc, received_crc);
        free(buffer);
        return NULL;
    }

    printf("[DEBUG] CRC verification passed\n");

    // ✅ Allocate memory for data frame structure
    data_frame_t *frame = malloc(sizeof(data_frame_t));
    if (!frame) {
        printf("[ERROR] Memory allocation failed for data frame structure\n");
        free(buffer);
        return NULL;
    }

    // ✅ Fill frame structure
    frame->address = frame_address;
    frame->function.custom = 0; // Not used in this format
    frame->frame_length = frame_length;
    frame->crc = received_crc;

    // ✅ Extract data payload (exclude address, length, and CRC)
    size_t data_len = frame_length - 4; // Total - Address - Length - CRC(2 bytes)
    printf("[DEBUG]   Data payload length: %zu bytes\n", data_len);

    if (data_len > 0) {
        frame->data = malloc(data_len);
        if (!frame->data) {
            printf("[ERROR] Memory allocation failed for data payload (%zu bytes)\n", data_len);
            free(frame);
            free(buffer);
            return NULL;
        }
        // Copy data from buffer[2] to buffer[frame_length-3]
        memcpy(frame->data, &buffer[2], data_len);
        
        printf("[DEBUG]   Data payload: ");
        for (size_t i = 0; i < data_len; i++) {
            printf("%02X ", ((uint8_t*)frame->data)[i]);
        }
        printf("\n");
    } else {
        frame->data = NULL;
        printf("[DEBUG]   No data payload\n");
    }

    printf("[DEBUG] Successfully parsed custom frame\n");
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