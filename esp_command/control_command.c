#include "control_command.h"

// Global variables for UART and semaphore (static to limit scope)
int uart_fd = -1;                        // UART file descriptor
sem_t *uart_sem = NULL;
/**
 * @brief Initializes the UART communication with thread-safe semephore protection.
 * 
 * This function sets up the UART parameters (baud rate, data bits, stop bits, parity)
 * and initializes a semephore for exclusive access in multithreaded environments.
 * It should be called once before any read/write operations.
 */
void Uart_Init(speed_t baudrate, char *device){
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
 * @brief Reads available response data from the UART with thread-safe semephore protection.
 * 
 * This function attempts to read as many bytes as available from the UART (up to a buffer limit)
 * and prints the received bytes in hexadecimal format. It ensures exclusive access using the semephore.
 * Additional processing logic can be added here if needed.
 */
uint16_t Read_Response(uint32_t timeout_ms) {
    if (sem_wait(uart_sem) != 0) return 0;
    if (uart_fd == -1) {
        sem_post(uart_sem);
        perror("UART not initialized or already closed");
        return 0;
    }
    // Use select to wait for data with a timeout
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(uart_fd, &read_fds);

    struct timeval timeout = { .tv_sec = 0, .tv_usec = timeout_ms * 1000 }; // timeout
    int ready = select(uart_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (ready < 0) { // Error in select
        perror("Select error");
        sem_post(uart_sem);
        return 0;
    } else if (ready == 0) { // Timeout, no data available
        sem_post(uart_sem);
        return 0;
    }
    // Data is available, read it
    unsigned char buffer[512] = {0};
    uint16_t bytes_read = read(uart_fd, buffer, sizeof(buffer));
    if (bytes_read > 0) {
        fprintf(stderr, "Response received (%u bytes): ", bytes_read);
        for (uint16_t i = 0; i < bytes_read; ++i) {
            fprintf(stderr, "%c", buffer[i]);
        }
        fprintf(stderr, "\n");
    } else if (bytes_read < 0) {
        perror("Read error");
    }
    
    sem_post(uart_sem);
    return bytes_read;
}

/**
 * @brief Writes a command to the UART with thread-safe semephore protection.
 * 
 * This function converts the Command enum to a single byte and sends it over UART.
 * It ensures exclusive access using the semephore.
 * 
 * @param cmd The Command enum value to send.
 */
void write_command(Command cmd) {
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
 * It locks the semephore for thread-safe access and prepares the command buffer.
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
            fprintf(stderr, "Unsupported baudrate: %u. Using default B115200.\n", baud_num);
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