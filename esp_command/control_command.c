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
 * Reads UART response with timeout and returns a pointer to the dynamically allocated buffer.
 * The caller must free the returned buffer after use to avoid memory leaks.
 * 
 * @param timeout_ms Timeout in milliseconds for waiting on data.
 * @param bytes_read_out Pointer to store the number of bytes read (output parameter).
 * @return Pointer to the allocated buffer containing the response, or NULL on error/timeout.
 */
unsigned char* Read_Response(uint32_t timeout_ms, uint16_t* bytes_read_out) {
    if (bytes_read_out == NULL) {
        // Invalid output parameter
        return NULL;
    }
    *bytes_read_out = 0;  // Initialize output to 0

    // Wait for semaphore
    if (sem_wait(uart_sem) != 0) return NULL;
    if (uart_fd == -1) {
        sem_post(uart_sem);
        perror("UART not initialized or already closed");
        return NULL;
    }

    // Prepare select for timeout
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(uart_fd, &read_fds);

    struct timeval timeout = { .tv_sec = 0, .tv_usec = timeout_ms * 1000 }; // Convert ms to us
    int ready = select(uart_fd + 1, &read_fds, NULL, NULL, &timeout);

    if (ready < 0) { // Select error
        perror("Select error");
        sem_post(uart_sem);
        return NULL;
    } else if (ready == 0) { // Timeout occurred
        sem_post(uart_sem);
        return NULL;
    }

    // Allocate dynamic buffer
    unsigned char* buffer = (unsigned char*)malloc(512);
    if (buffer == NULL) {
        perror("Malloc failed");
        sem_post(uart_sem);
        return NULL;
    }

    // Read available data
    ssize_t bytes_read = read(uart_fd, buffer, 512);  // Read up to 512 bytes
    if (bytes_read > 0) {
        *bytes_read_out = (uint16_t)bytes_read;  // Set output bytes read
        // Debug print to stderr
        fprintf(stderr, "Response received (%zd bytes): ", bytes_read);
    } else if (bytes_read < 0) {
        perror("Read error");
        free(buffer);  // Free on error
        buffer = NULL;
    } else {
        // No data read (bytes_read == 0)
        free(buffer);
        buffer = NULL;
    }

    // Release semaphore
    sem_post(uart_sem);
    return buffer;  // Return buffer pointer (caller must free)
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
            snprintf(stderr, "Unsupported baudrate: %u. Using default B115200.\n", baud_num);
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
        snprintf(fp, "%u\n%s", baud, dev);
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