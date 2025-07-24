#include "thread_func.h"

// ========== Shared state between threads ==========
volatile uint8_t is_busy = 0;

pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
int command_code = -1;
char status_response[100] = "Waiting for command...";
int status_color = 2;
unsigned char receive_data[512] = {0};
unsigned char save_data[512] = {0};
int shared_node_type = 0;  // Initialize to 0 (no node selected yet)

// ========== Menu definitions for each node ==========
static const char *node1_menu_items[] = {
    "1. Direction1",
    "2. Direction2",
    "3. Direction3",
    "4. Led_On",
    "5. Led_Off",
    "6. Send_Status",
    "7. Stop_System",
    "8. Init",
    "9. Re-flash firmware",
    "0. Exit program"
};

static const char *node2_menu_items[] = {
    "1. LED On",
    "2. LED Off",
    "3. Read Single",
    "4. Read Continuous",
    "0. Exit"
};

static int node1_menu_count = sizeof(node1_menu_items) / sizeof(node1_menu_items[0]);
static int node2_menu_count = sizeof(node2_menu_items) / sizeof(node2_menu_items[0]);

// ==================== UART THREAD ====================
void *uart_thread_func(void *arg) {
    uint16_t resp_len = 0;

    while (1) {
        pthread_mutex_lock(&command_mutex);
        int cmd = command_code;
        int pending = command_pending;
        command_pending = 0;  // Mark as handled
        int local_node_type = shared_node_type;  // Read shared node type safely
        pthread_mutex_unlock(&command_mutex);

        // Wait if node type not selected yet
        if (local_node_type == 0) {
            usleep(100 * 1000);  // Sleep and retry to avoid CPU spin
            continue;
        }

        if (!pending) {
            usleep(10 * 1000); // Avoid CPU spin
            continue;
        }

        int block_ui = (cmd == 1 || cmd == 2 || cmd == 3 || cmd == 6);
        if (block_ui)
            is_busy = 1;  // Block UI when handling these commands

        unsigned char *resp = NULL;
        int t1_val = 0, t2_val = 0, t3_val = 0;
        uint8_t ret = 0;

        // Handle commands based on selected_node_type
        if (local_node_type == NODE_TYPE_1) {
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
                    resp = Read_Response(100, &resp_len);
                    if (resp && resp_len > 0) {
                        strncpy(save_data, (const char *)resp, resp_len);
                        save_data[resp_len] = '\0';
                        // Parse T1, T2, T3
                        sscanf((const char*)save_data, "T1 (%d),T2 (%d),T3 (%d)", &t1_val, &t2_val, &t3_val);
                    }
                    break;
                case 7:
                    write_command(CMD_STOP_SYSTEM);
                    break;
                case 8:
                    write_init(115200);
                    break;
                case 9:  // CMD_REFLASH
                    is_busy = 1;
                    pthread_mutex_lock(&command_mutex);
                    snprintf(status_response, sizeof(status_response), "Flashing firmware...");
                    status_color = 4;
                    pthread_mutex_unlock(&command_mutex);

                    // Run shell command to flash
                    ret = system("bash -c 'source ../../../esptool-env/bin/activate && "
                                 "esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x10000 ../dcs-test.bin && "
                                 "deactivate'");
                    is_busy = 0;
                    break;
                default:
                    break;
            }
                // Update status (keep as is, adjust per node if needed)
        pthread_mutex_lock(&command_mutex);
        if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
            snprintf(status_response, sizeof(status_response), "Command %d executed successfully", cmd);
            snprintf(receive_data, sizeof(receive_data), "OK");
            status_color = 2;
        } 
        else if (resp && resp_len > 5 && cmd == 6 && local_node_type == NODE_TYPE_1) {
            snprintf(status_response, sizeof(status_response), "Command %d executed", cmd);
            snprintf(receive_data, sizeof(receive_data), "T1:%d,T2:%d,T3:%d", t1_val, t2_val, t3_val);
            status_color = 2;
        }
        else if (resp_len == 0 && (cmd == 4 || cmd == 5 || cmd == 7 || cmd == 8)) {
            snprintf(status_response, sizeof(status_response), "Command %d executed", cmd);
            snprintf(receive_data, sizeof(receive_data), "No response expected for command %d", cmd);
            status_color = 2;
        }
        else if (cmd == 9 && local_node_type == NODE_TYPE_1) {
            if (ret == 0) {
                snprintf(status_response, sizeof(status_response), "Firmware flashed successfully.");
                snprintf(receive_data, sizeof(receive_data), "esptool executed.");
                status_color = 2;
            } else {
                snprintf(status_response, sizeof(status_response), "Flashing failed!");
                snprintf(receive_data, sizeof(receive_data), "esptool error: return %d", ret);
                status_color = 3;
            }
        }
        else {
            snprintf(status_response, sizeof(status_response), "Command %d failed", cmd);
            snprintf(receive_data, sizeof(receive_data), "No response or error for command %d", cmd);
            status_color = 3;
        }
        pthread_mutex_unlock(&command_mutex);
        
        } else if (local_node_type == NODE_TYPE_2) {
            switch (cmd) {
                case 1:
                    write_command(CMD2_LED_ON);  // Assume defined in control_command.h
                    break;
                case 2:
                    write_command(CMD2_LED_OFF);
                    break;
                case 3:
                    write_command(CMD2_READ_SINGLE);
                    resp = Read_Response(100, &resp_len);  // Assume response
                    break;
                case 4:
                    write_command(CMD2_READ_CONTINUOUS);
                    resp = Read_Response(100, &resp_len);  // Assume response
                    break;
                default:
                    break;
            }
        }

        if (resp) {
            free(resp);
            resp_len = 0; // Reset response length
        }

        if (block_ui)
            is_busy = 0;  // Unblock UI when done
    }

    return NULL;
}

// ==================== UI THREAD ====================
void *ui_thread_func(void *arg) {
    // Receive arguments from main: baudrate, device
    uint32_t baudrate = *(uint32_t *)((void **)arg)[0];
    char *device = (char *)((void **)arg)[1];

    // Init ncurses
    initscr();
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    init_pair(5, COLOR_CYAN, COLOR_BLACK);
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    timeout(50);

    // ========== Node selection menu in ncurses ==========
    int selected_node_type = 0;
    int node_highlight = 0;
    const char *node_menu[] = {
        "1. Node1 - Motor Controller",
        "2. Node2 - Sensor Board"
    };
    int node_menu_count = sizeof(node_menu) / sizeof(node_menu[0]);
    int node_key;
    bool node_selected = false;

    while (!node_selected) {
        clear();
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "===== Select Node Type =====");
        attroff(COLOR_PAIR(4));

        for (int i = 0; i < node_menu_count; i++) {
            if (i == node_highlight)
                attron(COLOR_PAIR(1));
            mvprintw(2 + i, 0, "%s", node_menu[i]);
            if (i == node_highlight)
                attroff(COLOR_PAIR(1));
        }

        refresh();
        node_key = getch();

        switch (node_key) {
            case KEY_UP:
                node_highlight = (node_highlight == 0) ? node_menu_count - 1 : node_highlight - 1;
                break;
            case KEY_DOWN:
                node_highlight = (node_highlight == node_menu_count - 1) ? 0 : node_highlight + 1;
                break;
            case 10: // Enter
                selected_node_type = node_highlight + 1;
                if (selected_node_type == NODE_TYPE_1 || selected_node_type == NODE_TYPE_2) {
                    node_selected = true;
                } else {
                    // Display error message
                    attron(COLOR_PAIR(3));
                    mvprintw(5, 0, "Invalid selection. Press any key to try again.");
                    attroff(COLOR_PAIR(3));
                    refresh();
                    getch();
                }
                break;
        }
        usleep(50 * 1000);  // Redraw delay for node menu
    }

    // ========== Set dynamic menu based on selected node ==========
    const char **menu_items = NULL;
    int num_items = 0;
    if (selected_node_type == NODE_TYPE_1) {
        menu_items = node1_menu_items;
        num_items = node1_menu_count;
    } else if (selected_node_type == NODE_TYPE_2) {
        menu_items = node2_menu_items;
        num_items = node2_menu_count;
    }

    // ========== Notify UART thread of selected node type (via shared variable) ==========
    pthread_mutex_lock(&command_mutex);
    shared_node_type = selected_node_type;
    pthread_mutex_unlock(&command_mutex);

    // ========== Main UI LOOP ==========
    int highlight = 0;
    int key;

    while (1) {
        clear();
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== COMMAND SELECTION MENU ===");
        attroff(COLOR_PAIR(4));

        mvprintw(1, 0, "Current Baudrate: %u", baudrate);
        mvprintw(2, 0, "Current Device: %s", device);
        mvprintw(3, 0, "Current Node: %d", selected_node_type);  // Display selected node

        for (int i = 0; i < num_items; i++) {
            if (i == highlight)
                attron(COLOR_PAIR(1));
            mvprintw(5 + i, 0, "%s", menu_items[i]);
            if (i == highlight)
                attroff(COLOR_PAIR(1));
        }

        pthread_mutex_lock(&command_mutex);
        attron(COLOR_PAIR(status_color));
        mvprintw(5 + num_items + 2, 0, "Status: %s", status_response);
        attroff(COLOR_PAIR(status_color));
        attron(COLOR_PAIR(5)); // Received Data Color
        mvprintw(5 + num_items + 3, 0, "Received Data: %s", receive_data);
        attroff(COLOR_PAIR(5));

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

                // Handle exit based on node
                if ((selected_node_type == NODE_TYPE_1 && command_code == NODE1_EXIT_CODE) ||
                    (selected_node_type == NODE_TYPE_2 && command_code == NODE2_EXIT_CODE)) {
                    endwin();
                    printf("Exiting...\n");
                    close(uart_fd);
                    pthread_exit(NULL); // Exit UI thread
                }
                break;
        }

        usleep(50 * 1000); // Redraw delay
    }

    endwin();
    return NULL;
}
