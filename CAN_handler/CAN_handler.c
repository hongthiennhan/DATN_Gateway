#include "CAN_handler.h"
#include "main.h"

int can_fd = -1;
static pthread_mutex_t can_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize the CAN communication via USB to CAN
void CAN_Init(speed_t baudrate, char *device)
{
    if (pthread_mutex_lock(&can_mutex) != 0)
    {
        return;
    }

    // Close existing connection if open
    if (can_fd != -1)
    {
        close(can_fd);
    }

    can_fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (can_fd == -1)
    {
        perror("Failed to open Modbus device");
        pthread_mutex_unlock(&can_mutex);
        return;
    }

    struct termios options;
    if (tcgetattr(can_fd, &options) != 0)
    {
        perror("Failed to get Modbus UART attributes");
        close(can_fd);
        can_fd = -1;
        pthread_mutex_unlock(&can_mutex);
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

    tcflush(can_fd, TCIFLUSH); // Flush input buffer
    if (tcsetattr(can_fd, TCSANOW, &options) != 0)
    {
        perror("Failed to set CAN UART attributes");
        close(can_fd);
        can_fd = -1;
        pthread_mutex_unlock(&can_mutex);
        return;
    }
    pthread_mutex_unlock(&can_mutex);
}

// Write data to the CAN device
void CAN_Write_Data(uint8_t *data, size_t len)
{
    if (!data || len == 0)
    {
        return;
    }
    if (pthread_mutex_lock(&can_mutex) != 0)
    {
        return;
    }

    if (can_fd == -1)
    {
        pthread_mutex_unlock(&can_mutex);
        return;
    }

    ssize_t bytes_written = write(can_fd, data, len);
    if (bytes_written == -1)
    {
        perror("Failed to write to CAN");
    }

    pthread_mutex_unlock(&can_mutex);
}

// Read response from the CAN device
unsigned char *CAN_Read_Response(uint32_t timeout_ms, uint16_t *bytes_read_out)
{
    if (bytes_read_out == NULL)
        return NULL;
    *bytes_read_out = 0;

    // Acquire mutex for thread-safe CAN access
    if (pthread_mutex_lock(&can_mutex) != 0)
        return NULL;

    if (can_fd == -1)
    {
        pthread_mutex_unlock(&can_mutex);
        perror("CAN not initialized or already closed");
        return NULL;
    }

    // Get buffer size from config
    can_config_t *config = get_can_config();
    int buffer_size = config ? config->response_buffer_size : 2048; // fallback to 2048

    // Allocate buffer for response
    unsigned char *buffer = (unsigned char *)malloc(buffer_size);
    if (!buffer)
    {
        perror("Memory allocation failed");
        pthread_mutex_unlock(&can_mutex);
        return NULL;
    }

    ssize_t total_read = 0;
    uint32_t waited_ms = 0;
    uint32_t poll_interval = config ? config->poll_interval_ms : 20; // fallback to 20ms

    while (waited_ms < timeout_ms && total_read < buffer_size)
    {
        // Wait for CAN data to become available
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(can_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = poll_interval / 1000;
        timeout.tv_usec = (poll_interval % 1000) * 1000;

        int ready = select(can_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ready < 0)
        {
            perror("select() failed");
            break;
        }
        else if (ready == 0)
        {
            // No data yet; increment wait timer
            waited_ms += poll_interval;
            continue;
        }

        // Data available — read from CAN
        ssize_t r = read(can_fd, buffer + total_read, buffer_size - total_read);
        if (r > 0)
        {
            total_read += r;
            break;
        }
        else if (r < 0)
        {
            perror("read() failed");
            break;
        }
        // Else (r == 0): continue polling
    }

    *bytes_read_out = (uint16_t)total_read;
    if (total_read == 0)
    {
        // No data received
        free(buffer);
        buffer = NULL;
    }

    pthread_mutex_unlock(&can_mutex); // Release CAN access
    return buffer;
}

int Check_CAN_Data_Available(void)
{
    if (pthread_mutex_lock(&can_mutex) != 0)
        return 0;
    if (can_fd == -1)
    {
        pthread_mutex_unlock(&can_mutex);
        return 0;
    }
    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(can_fd, &readfds);

    // Short timeout to avoid blocking
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000; // 10ms

    int result = select(can_fd + 1, &readfds, NULL, NULL, &timeout);

    if (result > 0 && FD_ISSET(can_fd, &readfds))
    {
        pthread_mutex_unlock(&can_mutex);
        return 1; // Data available
    }

    pthread_mutex_unlock(&can_mutex);
    return 0; // No data
}

// Convert USB to CAN message to byte array
void CAN_USB_to_Byte(CAN_Message_USB_t *usb_msg, uint8_t *byte_array)
{
    if (!usb_msg || !byte_array || usb_msg->data_len > 8)
    {
        return;
    }
    byte_array[0] = usb_msg->header;
    byte_array[1] = usb_msg->command;
    byte_array[2] = usb_msg->id & 0xFF;        // Low byte of ID
    byte_array[3] = (usb_msg->id >> 8) & 0xFF; // High byte of ID
    for (int i = 0; i < usb_msg->data_len; i++)
    {
        byte_array[4 + i] = usb_msg->data[i];
    }
    byte_array[4 + usb_msg->data_len] = usb_msg->footer;
}

// Convert custom CAN message to byte array
void CAN_Custom_to_Byte(CAN_Message_t *can_msg, uint8_t *byte_array)
{
    if (!can_msg || !byte_array || can_msg->DLC > 8)
    {
        return;
    }
    byte_array[0] = can_msg->header;
    byte_array[1] = can_msg->ID & 0xFF;        // Low byte of ID
    byte_array[2] = (can_msg->ID >> 8) & 0xFF; // High byte of ID
    byte_array[3] = can_msg->RTR;
    byte_array[4] = can_msg->IDE;
    byte_array[5] = can_msg->reserved;
    byte_array[6] = can_msg->DLC;
    for (int i = 0; i < can_msg->DLC; i++)
    {
        byte_array[7 + i] = can_msg->data[i];
    }
}
