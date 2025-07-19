#include "main.h"
#include "control_command.h"

int main(int argc, char **argv) {
    int opt;
    int option_index = 0;
    uint32_t baudrate = 115200;  // Default baudrate
    char *device = "/dev/ttyUSB0";  // Default device

    // Try to load saved config
    load_config(&baudrate, &device);
    Uart_Init(map_to_speed(baudrate), device);

    while ((opt = getopt_long(argc, argv, "B:d:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 0:
                switch (option_index) {
                    case 0: // -- Direction1
                        write_command(CMD_DIRECTION_1);
                        break;
                    case 1:  // -- Direction2
                        write_command(CMD_DIRECTION_2);
                        break;
                    case 2:  // -- Direction3
                        write_command(CMD_DIRECTION_3);
                        break;
                    case 3:  // -- Relay_On
                        write_command(CMD_RELAY_ON);
                        break;
                    case 4:  // -- Relay_Off
                        write_command(CMD_RELAY_OFF);
                        break;
                    case 5:  // -- Led_On
                        write_command(CMD_LED_ON);
                        break;
                    case 6:  // -- Led_Off
                        write_command(CMD_LED_OFF);
                        break;
                    case 7:  // -- Send_Status
                        write_command(CMD_SEND_STATUS);
                        uint16_t response = Read_Response();
                        break;
                    case 8:  // -- Init
                    if (map_to_speed(baudrate) == B115200) baudrate = 115200;
                        write_init(baudrate);
                        break;
                    default:
                        fprintf(stderr, "Unknown long option.\n");
                        exit(1);
                }
                break;
            case 'B':  // -B for baudrate
                baudrate = atoi(optarg);
                if (map_to_speed(baudrate) == B115200) baudrate = 115200; // Ensure valid baudrate
                break;
            case 'd':  // -d for device
                device = optarg;
                break;
            case '?':  // Option not recognized!
                fprintf(stderr, "Usage: ./uart_app [--direction1] [--relay_on] etc. [-B<baud>] [-d<device>]\n");
                exit(1);
            default:
                break;
        }
    }
    close(uart_fd);
    return 0;
}
