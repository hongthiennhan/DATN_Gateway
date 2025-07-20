// Include necessary headers
#include "main.h"
#include "control_command.h"
#include <stdlib.h>
#include <string.h>

// Define long_options (simplified, as we only parse -B and -d)
static struct option long_options[] = {
    {0, 0, 0, 0}  // End of array
};

// Menu items
const char *menu_items[] = {
    "1. Direction1",
    "2. Direction2",
    "3. Direction3",
    "4. Led_On",
    "5. Led_Off",
    "6. Send_Status",
    "7. Stop_System",
    "8. Init",
    "0. Exit program"
};
int num_items = sizeof(menu_items) / sizeof(menu_items[0]);

int main(int argc, char **argv) {
    uint16_t response_bytes = 0;
    int opt;
    int option_index = 0;
    uint32_t baudrate = 115200;  // Default baudrate
    char *device = "/dev/ttyUSB0";  // Default device

    char receive_data_display[8096] = ""; // Always holds the latest received response

    // Parse command-line options for -B (baudrate) and -d (device)
    while ((opt = getopt_long(argc, argv, "B:d:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'B':
                baudrate = atoi(optarg);
                if (map_to_speed(baudrate) == B115200) baudrate = 115200;
                break;
            case 'd':
                device = optarg;
                break;
            case '?':
                fprintf(stderr, "Usage: ./main_app [-B <baudrate>] [-d <device>]\n");
                fprintf(stderr, "Example: ./main_app -B 9600 -d /dev/ttyUSB1\n");
                exit(1);
            default:
                break;
        }
    }

    load_config(&baudrate, &device);
    Uart_Init(map_to_speed(baudrate), device);
    Clear_Startup_UART(uart_fd, 10000); 

    // Ncurses init
    initscr();
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_WHITE); // Highlight
    init_pair(2, COLOR_GREEN, COLOR_BLACK); // Success
    init_pair(3, COLOR_RED, COLOR_BLACK);   // Error
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);// Header
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);

    char status[100] = "Welcome! Select a command.";
    int status_color = 2; // Green (success) by default

    int highlight = 0;
    int choice = 0;
    int key;

    while (1) {
        clear();
        // --- HEADER ---
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== COMMAND SELECTION MENU ===");
        attroff(COLOR_PAIR(4));
        mvprintw(1, 0, "Current Baudrate: %u", baudrate);
        mvprintw(2, 0, "Current Device: %s", device);

        // --- MENU ---
        for (int i = 0; i < num_items; i++) {
            if (i == highlight)
                attron(COLOR_PAIR(1));
            mvprintw(4 + i, 0, "%s", menu_items[i]);
            if (i == highlight)
                attroff(COLOR_PAIR(1));
        }

        // --- STATUS ---
        attron(COLOR_PAIR(status_color));
        mvprintw(4 + num_items + 1, 0, "Status: %s", status);
        attroff(COLOR_PAIR(status_color));

        refresh();

        // --- USER INPUT ---
        key = getch();
        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? num_items - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == num_items - 1) ? 0 : highlight + 1;
                break;
            case 10: // Enter key
                choice = highlight + 1;
                status_color = 2; // Success as default

                switch (choice) {
                    case 1: { // Direction1
                        write_command(CMD_DIRECTION_1);
                        unsigned char *response = Read_Response(10000, &response_bytes);
                        if (response && response_bytes >= 2 && strncmp((const char*)response, "OK", 2) == 0) {
                            snprintf(status, sizeof(status), "Direction1 executed successfully");
                        } else {
                            snprintf(status, sizeof(status), "Direction1 failed: Invalid response");
                            status_color = 3;
                        }
                        if (response && response_bytes > 0) {
                            strncpy(receive_data_display, (const char*)response, response_bytes);
                            receive_data_display[response_bytes] = '\0';
                        }
                        if (response_bytes > 0) free(response);
                        break;
                    }
                    case 2: { // Direction2
                        write_command(CMD_DIRECTION_2);
                        unsigned char *response = Read_Response(5000, &response_bytes);
                        if (response && response_bytes >= 2 && strncmp((const char*)response, "OK", 2) == 0) {
                            snprintf(status, sizeof(status), "Direction2 executed successfully");
                        } else {
                            snprintf(status, sizeof(status), "Direction2 failed: Invalid response");
                            status_color = 3;
                        }
                        if (response && response_bytes > 0) {
                            strncpy(receive_data_display, (const char*)response, response_bytes);
                            receive_data_display[response_bytes] = '\0';
                        }
                        if (response_bytes > 0) free(response);
                        break;
                    }
                    case 3: { // Direction3
                        write_command(CMD_DIRECTION_3);
                        unsigned char *response = Read_Response(10000, &response_bytes);
                        if (response && response_bytes >= 2 && strncmp((const char*)response, "OK", 2) == 0) {
                            snprintf(status, sizeof(status), "Direction3 executed successfully");
                        } else {
                            snprintf(status, sizeof(status), "Direction3 failed: Invalid response");
                            status_color = 3;
                        }
                        if (response && response_bytes > 0) {
                            strncpy(receive_data_display, (const char*)response, response_bytes);
                            receive_data_display[response_bytes] = '\0';
                        }
                        if (response_bytes > 0) free(response);
                        break;
                    }
                    case 4: // Led_On
                        write_command(CMD_LED_ON);
                        snprintf(status, sizeof(status), "Led_On executed successfully");
                        // No new UART data, do not update receive_data_display
                        break;
                    case 5: // Led_Off
                        write_command(CMD_LED_OFF);
                        snprintf(status, sizeof(status), "Led_Off executed successfully");
                        // No new UART data, do not update receive_data_display
                        break;
                    case 6: { // Send_Status
                        write_command(CMD_SEND_STATUS);
                        unsigned char *response = Read_Response(1000, &response_bytes);
                        if (response && response_bytes > 0) {
                            snprintf(status, sizeof(status), "Send_Status executed successfully");
                            strncpy(receive_data_display, (const char*)response, response_bytes);
                            receive_data_display[response_bytes] = '\0';
                        } else {
                            snprintf(status, sizeof(status), "Send_Status failed: No response");
                            status_color = 3;
                            // Không xóa receive_data_display: giữ lại giá trị cũ như yêu cầu
                        }
                        if (response_bytes > 0) free(response);
                        break;
                    }
                    case 7: // Stop_System
                        write_command(CMD_STOP_SYSTEM);
                        snprintf(status, sizeof(status), "Stop_System executed successfully");
                        break;
                    case 8: // Init
                        if (map_to_speed(baudrate) == B115200) baudrate = 115200;
                        write_init(baudrate);
                        snprintf(status, sizeof(status), "Init executed successfully");
                        break;
                    case 9: // Exit
                        endwin();
                        printf("Exiting program...\n");
                        close(uart_fd);
                        return 0;
                    default:
                        snprintf(status, sizeof(status), "Invalid choice! Please select again.");
                        status_color = 3;
                        break;
                }
                break;
        }
    }

    endwin();
    close(uart_fd);
    return 0;
}
