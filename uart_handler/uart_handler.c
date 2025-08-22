#include "uart_handler.h"
#include "node_config.h"

// Global variables for UART and mutex (static to limit scope)
int uart_fd = -1;
pthread_mutex_t uart_mutex = PTHREAD_MUTEX_INITIALIZER;

void Uart_Init(speed_t baudrate, char *device) {
    if (pthread_mutex_lock(&uart_mutex) != 0) {
        return;
    }

    uart_fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        perror("Failed to open UART device");
        pthread_mutex_unlock(&uart_mutex);
        return;
    }

    struct termios options;
    if (tcgetattr(uart_fd, &options) != 0) {
        perror("Failed to get UART attributes");
        close(uart_fd);
        uart_fd = -1;
        pthread_mutex_unlock(&uart_mutex);
        return;
    }

    // Set baud rate, data bits, parity, stop bits, and other flags
    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);
    options.c_cflag = (options.c_cflag & ~CSIZE) | CS8; // 8 data bits
    options.c_cflag &= ~PARENB; // No parity
    options.c_cflag &= ~CSTOPB; // 1 stop bit
    options.c_cflag |= CLOCAL | CREAD; // Ignore modem control lines, enable receiver
    options.c_iflag = IGNPAR; // Ignore parity errors
    options.c_oflag = 0; // No output processing
    options.c_lflag = 0; // No input processing

    tcflush(uart_fd, TCIFLUSH); // Flush input buffer
    if (tcsetattr(uart_fd, TCSANOW, &options) != 0) {
        perror("Failed to set UART attributes");
        close(uart_fd);
        uart_fd = -1;
        pthread_mutex_unlock(&uart_mutex);
        return;
    }
    pthread_mutex_unlock(&uart_mutex);
}

unsigned char* UART_Read_Response(uint32_t timeout_ms, uint16_t* bytes_read_out) {
    if (bytes_read_out == NULL) return NULL;
    *bytes_read_out = 0;

    // Acquire mutex for thread-safe UART access
    if (pthread_mutex_lock(&uart_mutex) != 0) return NULL;

    if (uart_fd == -1) {
        pthread_mutex_unlock(&uart_mutex);
        perror("UART not initialized or already closed");
        return NULL;
    }

    // Get buffer size from config
    uart_config_t *config = get_uart_config();
    int buffer_size = config ? config->response_buffer_size : 2048; // fallback to 2048
    
    // Allocate buffer for response
    unsigned char* buffer = (unsigned char*)malloc(buffer_size);
    if (!buffer) {
        perror("Memory allocation failed");
        pthread_mutex_unlock(&uart_mutex);
        return NULL;
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;
    uint32_t poll_interval = config ? config->poll_interval_ms : 20; // fallback to 20ms

    while (waited_ms < timeout_ms && total_read < buffer_size) {
        // Wait for UART data to become available
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(uart_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = poll_interval / 1000;
        timeout.tv_usec = (poll_interval % 1000) * 1000;

        int ready = select(uart_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0) {
            perror("select() failed");
            break;
        } else if (ready == 0) {
            // No data yet; increment wait timer
            waited_ms += poll_interval;
            continue;
        }

        // Data available — read from UART
        ssize_t r = read(uart_fd, buffer + total_read, buffer_size - total_read);
        if (r > 0) {
            total_read += r;
            break;
        } else if (r < 0) {
            perror("read() failed");
            break;
        }
        // Else (r == 0): continue polling
    }

    *bytes_read_out = (uint16_t)total_read;
    if (total_read == 0) {
        // No data received
        free(buffer);
        buffer = NULL;
    }

    pthread_mutex_unlock(&uart_mutex); // Release UART access
    return buffer;
}

void UART_Write_Command(uint8_t cmd) {
    if (pthread_mutex_lock(&uart_mutex) != 0) return;
    if (uart_fd == -1) {
        pthread_mutex_unlock(&uart_mutex);
        return;
    }
    uint16_t bytes_written = write(uart_fd, &cmd, 1);
    pthread_mutex_unlock(&uart_mutex);
}

void UART_Write_Data(uint8_t *data, size_t len) {
    if (pthread_mutex_lock(&uart_mutex) != 0) return;
    if (uart_fd == -1) {
        pthread_mutex_unlock(&uart_mutex);
        return;
    }
    uint16_t bytes_written = write(uart_fd, data, len);
    pthread_mutex_unlock(&uart_mutex);
}

speed_t UART_map_to_speed(uint32_t baud_num) {
    uart_config_t *config = get_uart_config();
    speed_t speed = B115200; // Default speed
    uint8_t check = 1;
    if (config && config->supported_baudrates) {
        // Use config-driven mapping
        for (int i = 0; i < config->baudrate_count; i++) {
            if (config->supported_baudrates[i].rate == baud_num) {
                // Convert string speed code to actual speed_t value
                const char *speed_code = config->supported_baudrates[i].speed_code;
#ifdef DEBUG
                printf("Mapping baud rate %u to speed code %s\n", baud_num, speed_code);
#endif
                if (strcmp(speed_code, "B9600") == 0) return B9600;
                if (strcmp(speed_code, "B19200") == 0) return B19200;
                if (strcmp(speed_code, "B38400") == 0) return B38400;
                if (strcmp(speed_code, "B57600") == 0) return B57600;
                if (strcmp(speed_code, "B115200") == 0) return B115200;
                if (strcmp(speed_code, "B230400") == 0) return B230400;
                if (strcmp(speed_code, "B460800") == 0) return B460800;
                if (strcmp(speed_code, "B921600") == 0) return B921600;
            }
        }
    }
    
    // Fallback to default if not found in config
    int fallback_rate = config ? config->default_baudrate_fallback : 115200;
    
    // Get buffer size from config for error message
    int error_buf_size = config ? config->error_message_buffer_size : 100;
    char *error_msg = malloc(error_buf_size);
    if (error_msg) {
        snprintf(error_msg, error_buf_size, "Unsupported baudrate: %u. Using default %d.\n", baud_num, fallback_rate);
        fputs(error_msg, stderr);
        free(error_msg);
    }
    
    return B115200; // Default fallback
}

void Clear_Startup_UART(int fd, uint32_t flush_duration_ms) {
    uart_config_t *config = get_uart_config();
    int temp_buf_size = config ? config->temp_buffer_size : 256;
    uint32_t interval_ms = config ? config->flush_interval_ms : 50;
    
    unsigned char *temp_buf = malloc(temp_buf_size);
    if (!temp_buf) {
        return; // Cannot allocate buffer
    }
    
    fd_set read_fds;
    struct timeval timeout;
    uint32_t waited_ms = 0;

    // Non-blocking read loop for flush_duration_ms
    while (waited_ms < flush_duration_ms) {
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        
        timeout.tv_sec = 0;
        timeout.tv_usec = interval_ms * 1000;

        int ready = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(fd, &read_fds)) {
            ssize_t r = read(fd, temp_buf, temp_buf_size);
            if (r > 0) {
                // Optionally dump data to log
                // fwrite(temp_buf, 1, r, stderr);
            }
        }
        waited_ms += interval_ms;
    }

    free(temp_buf);
    // Ensure buffer is flushed at end
    tcflush(fd, TCIFLUSH);
}

// non-blocking UART data check:
int Check_UART_Data_Available(void) {
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(uart_fd, &readfds);
    
    // Short timeout to avoid blocking
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; // 10ms
    
    int result = select(uart_fd + 1, &readfds, NULL, NULL, &timeout);
    
    if (result > 0 && FD_ISSET(uart_fd, &readfds)) {
        return 1; // Data available
    }
    
    return 0; // No data
}
