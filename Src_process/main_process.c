#include "main.h"
#include "control_command.h"

// ========== Menu & Options ==========
static struct option long_options[] = {
    {0, 0, 0, 0}
};

volatile uint8_t is_busy = 0;

static const char *menu_items[] = {
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

// ========== Shared state between threads ==========
pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
int command_code = -1;
char status_response[100] = "Waiting for command...";
int status_color = 2;

// ==================== UART THREAD ====================
void *uart_thread_func(void *arg) {
    uint16_t resp_len = 0;

    while (1) {
        pthread_mutex_lock(&command_mutex);
        int cmd = command_code;
        int pending = command_pending;
        command_pending = 0;  // Mark as handled
        pthread_mutex_unlock(&command_mutex);

        if (!pending) {
            usleep(10 * 1000); // Avoid CPU spin
            continue;
        }

        int block_ui = (cmd == 1 || cmd == 2 || cmd == 3 || cmd == 6);
        if (block_ui)
            is_busy = 1;  //Block UI when handling these commands

        unsigned char *resp = NULL;

        switch (cmd) {
            case 1:
                write_command(CMD_DIRECTION_1);
                resp = Read_Response(10000, &resp_len);
                break;
            case 2:
                write_command(CMD_DIRECTION_2);
                resp = Read_Response(5000, &resp_len);
                break;
            case 3:
                write_command(CMD_DIRECTION_3);
                resp = Read_Response(10000, &resp_len);
                break;
            case 4:
                write_command(CMD_LED_ON);
                break;
            case 5:
                write_command(CMD_LED_OFF);
                break;
            case 6:
                write_command(CMD_SEND_STATUS);
                resp = Read_Response(1000, &resp_len);
                break;
            case 7:
                write_command(CMD_STOP_SYSTEM);
                break;
            case 8:
                write_init(115200);
                break;
            default:
                break;
        }

        // Update status
        pthread_mutex_lock(&command_mutex);
        if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
            snprintf(status_response, sizeof(status_response), "Command %d executed successfully", cmd);
            status_color = 2;
        } 
        else if (resp && resp_len > 0 && cmd == 6) {
            snprintf(status_response, sizeof(status_response), "Command %d executed", cmd);
            status_color = 2;
        } 
        else if (resp_len == 0 && (cmd == 4 || cmd == 5 || cmd == 7 || cmd == 8)) {
            snprintf(status_response, sizeof(status_response), "Command %d executed", cmd);
            status_color = 2;
        } 
        else {
            snprintf(status_response, sizeof(status_response), "Command %d failed", cmd);
            status_color = 3;
        }
        pthread_mutex_unlock(&command_mutex);
        if (resp) {
            free(resp);
            resp_len = 0; // Reset response length
        }

        if (block_ui)
            is_busy = 0;  // Unblock UI when done
    }

    return NULL;
}

// ==================== MAIN FUNCTION / UI Thread ====================
int main(int argc, char **argv) {
    uint32_t baudrate = 115200;
    uint16_t response_len = 0;
    char *device = "/dev/ttyUSB0";
    int opt, option_index = 0;

    // Parse command line
    while ((opt = getopt_long(argc, argv, "B:d:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'B':
                baudrate = atoi(optarg);
                if (map_to_speed(baudrate) == B0) baudrate = 115200;
                break;
            case 'd':
                device = optarg;
                break;
            case '?':
                fprintf(stderr, "Usage: %s [-B baudrate] [-d device]\n", argv[0]);
                exit(1);
            default:
                break;
        }
    }

    load_config(&baudrate, &device);
    Uart_Init(map_to_speed(baudrate), device);
    Clear_Startup_UART(uart_fd, 10000);

    // Start UART thread
    pthread_t uart_thread;
    pthread_create(&uart_thread, NULL, uart_thread_func, NULL);

    // Init ncurses
    initscr();
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    timeout(300);

    int highlight = 0;
    int key;

    // ========== UI LOOP ==========
    while (1) {
        clear();
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== COMMAND SELECTION MENU ===");
        attroff(COLOR_PAIR(4));

        mvprintw(1, 0, "Current Baudrate: %u", baudrate);
        mvprintw(2, 0, "Current Device: %s", device);

        for (int i = 0; i < num_items; i++) {
            if (i == highlight)
                attron(COLOR_PAIR(1));
            mvprintw(4 + i, 0, "%s", menu_items[i]);
            if (i == highlight)
                attroff(COLOR_PAIR(1));
        }

        pthread_mutex_lock(&command_mutex);
        attron(COLOR_PAIR(status_color));
        mvprintw(4 + num_items + 2, 0, "Status: %s", status_response);
        attroff(COLOR_PAIR(status_color));
        pthread_mutex_unlock(&command_mutex);

        refresh();

        key = getch();
        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? num_items - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == num_items - 1) ? 0 : highlight + 1;
                break;
            case 10: // Enter
                if (is_busy) {
                    snprintf(status_response, sizeof(status_response), "Busy: Please wait for command to finish");
                    status_color = 3;
                    break;
                }
                pthread_mutex_lock(&command_mutex);
                command_code = highlight + 1;
                command_pending = 1;
                pthread_mutex_unlock(&command_mutex);

                if (command_code == 9) { // Exit
                    endwin();
                    printf("Exiting...\n");
                    close(uart_fd);
                    return 0;
                }
                break;
        }

        usleep(50 * 1000); // Redraw delay
    }

    endwin();
    close(uart_fd);
    return 0;
}
