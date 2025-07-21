#include "control_command.h"
#include <stdio.h>   // For fprintf, fputs
#include <stdlib.h>  // For malloc/free
#include <string.h>  // For strlen

// Global variables for UART and semaphore (static to limit scope)
int uart_fd = -1;                        // UART file descriptor
sem_t *uart_sem = NULL;

/**
 * @brief Initializes the UART communication with thread-safe semaphore protection.
 * 
 * This function sets up the UART parameters (baud rate, data bits, stop bits, parity)
 * and initializes a semaphore for exclusive access in multithreaded environments.
 * It should be called once before any read/write operations.
 */
void Uart_Init(speed_t baudrate, char *device) {
    uart_sem = sem_open("/uart_sem", O_CREAT, 0644, 1);
    if (uart_sem == SEM_FAILED) {
        perror("sem_open failed");
        return;
    }

    if (sem_wait(uart_sem) != 0) {
        return;
    }

    uart_fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        perror("Failed to open UART device");
        sem_post(uart_sem);
        return;
    }

    struct termios options;
    if (tcgetattr(uart_fd, &options) != 0) {
        perror("Failed to get UART attributes");
        close(uart_fd);
        uart_fd = -1;
        sem_post(uart_sem);
        return;
    }

    // Set baud rate, data bits, parity, stop bits, and other flags
    cfsetispeed(&options, baudrate);
    cfsetospeed(&options, baudrate);
    options.c_cflag = (options.c_cflag & ~CSIZE) | CS8;  // 8 data bits
    options.c_cflag &= ~PARENB;                          // No parity
    options.c_cflag &= ~CSTOPB;                          // 1 stop bit
    options.c_cflag |= CLOCAL | CREAD;                   // Ignore modem control lines, enable receiver
    options.c_iflag = IGNPAR;                            // Ignore parity errors
    options.c_oflag = 0;                                 // No output processing
    options.c_lflag = 0;                                 // No input processing
    tcflush(uart_fd, TCIFLUSH);                          // Flush input buffer

    if (tcsetattr(uart_fd, TCSANOW, &options) != 0) {
        perror("Failed to set UART attributes");
        close(uart_fd);
        uart_fd = -1;
        sem_post(uart_sem);
        return;
    }

    sem_post(uart_sem);
}

/**
 * Reads UART response with timeout and returns a pointer to the dynamically allocated buffer.
 * The caller must free the returned buffer after use to avoid memory leaks.
 * 
 * @param timeout_ms Timeout in milliseconds for waiting on data.
 * @param bytes_read_out Pointer to store the number of bytes read (output parameter).
 * @return Pointer to the allocated buffer containing the response, or NULL on error/timeout.
 */
unsigned char* Read_Response(uint32_t timeout_ms, uint16_t* bytes_read_out) {
    if (bytes_read_out == NULL) return NULL;
    *bytes_read_out = 0;

    // Acquire semaphore for thread-safe UART access
    if (sem_wait(uart_sem) != 0) return NULL;
    if (uart_fd == -1) {
        sem_post(uart_sem);
        perror("UART not initialized or already closed");
        return NULL;
    }

    // Allocate buffer for response
    unsigned char* buffer = (unsigned char*)malloc(2048); // Allocate 2048 bytes
    if (!buffer) {
        perror("Memory allocation failed");
        sem_post(uart_sem);
        return NULL;
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;
    const uint32_t poll_interval = 20; // Check every 20 ms

    while (waited_ms < timeout_ms && total_read < 2048) {
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
        ssize_t r = read(uart_fd, buffer + total_read, 2048 - total_read);
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

    sem_post(uart_sem); // Release UART access
    return buffer;
}

/**
 * @brief Writes a command to the UART with thread-safe semaphore protection.
 * 
 * This function converts the Command enum to a single byte and sends it over UART.
 * It ensures exclusive access using the semaphore.
 * 
 * @param cmd The Command enum value to send.
 */
void write_command(uint8_t cmd) {
    if (sem_wait(uart_sem) != 0) return;
    if (uart_fd == -1) {
        sem_post(uart_sem);
        return;
    }

    unsigned char byte_cmd = (unsigned char)cmd;
    uint16_t bytes_written = write(uart_fd, &byte_cmd, 1);

    sem_post(uart_sem);
}

/**
 * @brief Writes an initialization command with baudrate to the UART.
 * 
 * This function sends a command to initialize the UART with a specific baudrate.
 * It locks the semaphore for thread-safe access and prepares the command buffer.
 * 
 * @param baudrate The baud rate to set for the UART.
 */
void write_init(uint32_t baudrate) {
    if (sem_wait(uart_sem) != 0) return;
    if (uart_fd == -1) {
        sem_post(uart_sem);
        return;
    }

    uint8_t buffer[5];
    buffer[0] = CMD_INIT;
    buffer[1] = (uint8_t)(baudrate & 0xFF);
    buffer[2] = (uint8_t)((baudrate >> 8) & 0xFF);
    buffer[3] = (uint8_t)((baudrate >> 16) & 0xFF);
    buffer[4] = (uint8_t)((baudrate >> 24) & 0xFF);
    uint16_t bytes_written = write(uart_fd, buffer, 5);

    sem_post(uart_sem);
}

/**
 * @brief Maps a baud rate number to the corresponding speed_t value.
 * 
 * This function converts a standard baud rate number to the appropriate speed_t value
 * used in termios settings. It handles common baud rates and returns a default value
 * for unsupported rates.
 * 
 * @param baud_num The baud rate number to map.
 * @return The corresponding speed_t value.
 */
speed_t map_to_speed(uint32_t baud_num) {
    switch (baud_num) {
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
        case 230400:  return B230400;
        case 460800:  return B460800;
        case 921600:  return B921600;
        default:
            // Use snprintf for error message
            char error_msg[100];
            snprintf(error_msg, sizeof(error_msg), "Unsupported baudrate: %u. Using default B115200.\n", baud_num);
            fputs(error_msg, stderr);  // Print to stderr
            return B115200;
    }
}

/**
 * @brief Saves UART configuration to a file.
 * 
 * This function writes the current baud rate and device name to a configuration file.
 * It can be used to persist settings across application runs.
 * 
 * @param baud The baud rate to save.
 * @param dev The device name to save.
 */
void save_config(uint32_t baud, const char *dev) {
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (fp) {
        // Use fprintf instead of snprintf for direct file writing
        fprintf(fp, "%u\n%s", baud, dev);
        fclose(fp);
    }
}

/**
 * @brief Loads UART configuration from a file.
 * 
 * This function reads the baud rate and device name from a configuration file.
 * It returns 1 on success and 0 on failure, allowing the caller to handle errors.
 * 
 * @param baud Pointer to store the baud rate.
 * @param dev Pointer to store the device name.
 * @return 1 if successful, 0 if failed.
 */
uint8_t load_config(uint32_t *baud, char **dev) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return 0;
    char buf[256];
    if (fscanf(fp, "%u\n%s", baud, buf) != 2) {
        fclose(fp);
        return 0;
    }
    *dev = strdup(buf);
    fclose(fp);
    return 1;
}

/**
 * @brief Clears the UART input buffer for a specified duration.
 * 
 * This function reads from the UART file descriptor to clear any existing data
 * in the input buffer. It uses select() to wait for data and reads in chunks
 * until the specified flush duration is reached.
 * 
 * @param fd The file descriptor of the UART device.
 * @param flush_duration_ms Duration in milliseconds to flush the buffer.
 */
void Clear_Startup_UART(int fd, uint32_t flush_duration_ms) {
    unsigned char temp_buf[256];
    fd_set read_fds;

    struct timeval timeout;
    uint32_t waited_ms = 0;
    const uint32_t interval_ms = 50;  // check every 50ms

    // Non-blocking read loop for flush_duration_ms
    while (waited_ms < flush_duration_ms) {
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        timeout.tv_sec = 0;
        timeout.tv_usec = interval_ms * 1000;

        int ready = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(fd, &read_fds)) {
            ssize_t r = read(fd, temp_buf, sizeof(temp_buf));
            if (r > 0) {
                // Optionally dump data to log
                // fwrite(temp_buf, 1, r, stderr);
            }
        }

        waited_ms += interval_ms;
    }

    // Ensure buffer is flushed at end
    tcflush(fd, TCIFLUSH);
}