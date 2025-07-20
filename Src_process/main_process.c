// Include necessary headers
#include "main.h"
#include "control_command.h"

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

// Main function: Handles command-line options and ncurses-based interactive menu with colors
int main(int argc, char **argv) {
    int opt;
    int option_index = 0;
    uint32_t baudrate = 115200;  // Default baudrate
    char *device = "/dev/ttyUSB0";  // Default device

    // Parse command-line options for -B (baudrate) and -d (device)
    while ((opt = getopt_long(argc, argv, "B:d:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'B':  // Set baudrate from argument
                baudrate = atoi(optarg);
                if (map_to_speed(baudrate) == B115200) baudrate = 115200; // Ensure valid baudrate
                break;
            case 'd':  // Set device from argument
                device = optarg;
                break;
            case '?':  // Handle unrecognized option
                fprintf(stderr, "Usage: ./main_app [-B <baudrate>] [-d <device>]\n");
                fprintf(stderr, "Example: ./main_app -B 9600 -d /dev/ttyUSB1\n");
                exit(1);
            default:
                break;
        }
    }

    // Load saved config (overridden by command-line options if provided)
    load_config(&baudrate, &device);
    // Initialize UART with current baudrate and device
    Uart_Init(map_to_speed(baudrate), device);

    // Initialize ncurses
    initscr();            // Start ncurses mode
    start_color();        // Enable colors
    init_pair(1, COLOR_BLACK, COLOR_WHITE); // Highlight: black on white
    init_pair(2, COLOR_GREEN, COLOR_BLACK); // Success status: green on black
    init_pair(3, COLOR_RED, COLOR_BLACK);   // Error status: red on black
    init_pair(4, COLOR_YELLOW, COLOR_BLACK); // Header: yellow on black
    keypad(stdscr, TRUE); // Enable keypad (arrow keys)
    noecho();             // Don't echo input
    curs_set(0);          // Hide cursor

    // Status buffer
    char status[100] = "Welcome! Select a command.";
    int status_color = 2; // Default to success color

    // Display initial config and menu
    int highlight = 0; // Current highlighted item
    int choice = 0;
    int key;

    while (1) {
        clear(); // Clear screen

        // Display header with color
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== COMMAND SELECTION MENU ===");
        attroff(COLOR_PAIR(4));
        mvprintw(1, 0, "Current Baudrate: %u", baudrate);
        mvprintw(2, 0, "Current Device: %s", device);

        // Display menu items with highlight
        for (int i = 0; i < num_items; i++) {
            if (i == highlight) {
                attron(COLOR_PAIR(1)); // Highlight color
            }
            mvprintw(4 + i, 0, "%s", menu_items[i]);
            if (i == highlight) {
                attroff(COLOR_PAIR(1));
            }
        }

        // Display status with color
        attron(COLOR_PAIR(status_color));
        mvprintw(4 + num_items + 1, 0, "Status: %s", status);
        attroff(COLOR_PAIR(status_color));

        refresh(); // Refresh screen

        // Get user input
        key = getch();
        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? num_items - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == num_items - 1) ? 0 : highlight + 1;
                break;
            case 10: // Enter key
                choice = highlight + 1; // Map to 1-9 (9 for exit)
                status_color = 2; // Default to success
                // Handle choice
                switch (choice) {
                    case 1:
                        write_command(CMD_DIRECTION_1);
                        Read_Response(5000);
                        snprintf(status, sizeof(status), "Direction1 executed successfully");
                        break;
                    case 2:
                        write_command(CMD_DIRECTION_2);
                        Read_Response(5000);
                        snprintf(status, sizeof(status), "Direction2 executed successfully");
                        break;
                    case 3:
                        write_command(CMD_DIRECTION_3);
                        Read_Response(5000);
                        snprintf(status, sizeof(status), "Direction3 executed successfully");
                        break;
                    case 4:
                        write_command(CMD_LED_ON);
                        snprintf(status, sizeof(status), "Led_On executed successfully");
                        break;
                    case 5:
                        write_command(CMD_LED_OFF);
                        snprintf(status, sizeof(status), "Led_Off executed successfully");
                        break;
                    case 6:
                        write_command(CMD_SEND_STATUS);
                        Read_Response(100);
                        snprintf(status, sizeof(status), "Send_Status executed successfully");
                        break;
                    case 7:
                        write_command(CMD_STOP_SYSTEM);
                        snprintf(status, sizeof(status), "Stop_System executed successfully");
                        break;
                    case 8:
                        if (map_to_speed(baudrate) == B115200) baudrate = 115200;
                        write_init(baudrate);
                        snprintf(status, sizeof(status), "Init executed successfully");
                        break;
                    case 9: // Exit
                        endwin(); // End ncurses
                        printf("Exiting program...\n");
                        close(uart_fd);
                        return 0;
                    default:
                        snprintf(status, sizeof(status), "Invalid choice! Please select again.");
                        status_color = 3; // Error color
                        break;
                }
                break;
        }
    }

    endwin(); // End ncurses (fallback)
    close(uart_fd);
    return 0;
}
