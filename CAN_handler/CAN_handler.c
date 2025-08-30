#include "CAN_handler.h"
#include "main.h"

int can_fd = -1;
static pthread_mutex_t can_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize the CAN communication via USB to CAN
void CAN_Init(speed_t baudrate, char *device) {
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
void CAN_Write_Data(uint8_t *data, size_t len) {
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
unsigned char* CAN_Read_Response(uint32_t timeout_ms) {

    return NULL;
}

int Check_CAN_Data_Available(void) {
    // Code to check if CAN data is available
    return 0;
}

// Convert USB to CAN message to byte array
void CAN_USB_to_Byte(CAN_Message_USB *usb_msg, uint8_t *byte_array) {
    byte_array[0] = usb_msg->header;
    byte_array[1] = usb_msg->command;
    byte_array[2] = usb_msg->id & 0xFF;         // Low byte of ID
    byte_array[3] = (usb_msg->id >> 8) & 0xFF;  // High byte of ID
    for (int i = 0; i < usb_msg->data_length; i++) {
        byte_array[4 + i] = usb_msg->data[i];
    }
    byte_array[4 + usb_msg->data_length] = usb_msg->footer;
}

void CAN_to_Byte(CAN_Message *can_msg, uint8_t *byte_array) {
    // Code to convert CAN message to byte array
}
