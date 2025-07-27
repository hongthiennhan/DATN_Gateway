#include "thread_func.h"
#include <time.h>

// ========== Shared state between threads ==========
volatile uint8_t is_busy = 0;

pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
char status_response[100] = "Waiting for command...";
int status_color = 2;
unsigned char receive_data[512] = {0};
unsigned char save_data[512] = {0};
int shared_node_type = 0;  // Initialize to 0 (no node selected yet)

// ADDED: Auto read functionality
#define AUTO_READ_COMMAND 100  // Special command code for auto read
volatile time_t last_user_interaction = 0;  // Track last user interaction time
#define AUTO_READ_INTERVAL 1  // Auto read every 1 second

shared_data_t command_data = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Helper functions for command_data operations:
// Set the command code and notify waiting threads
void set_command_code(int new_code) { // use only for setting command code
    pthread_mutex_lock(&command_data.mutex);
    if (command_data.data == NULL) {
        command_data.data = malloc(sizeof(int));
    }
    *(int*)command_data.data = new_code;
    pthread_cond_signal(&command_data.cond);  // Notify waiting threads
    pthread_mutex_unlock(&command_data.mutex);
}

// Get the current command code
int get_command_code() { // use only for getting command code
    pthread_mutex_lock(&command_data.mutex);
    int code = 0;
    if (command_data.data != NULL) {
        code = *(int*)command_data.data;
    }
    pthread_mutex_unlock(&command_data.mutex);
    return code;
}

// Wait for command change with timeout
// Returns the command code if signaled, or -1 if timeout occurs
int wait_for_command_change(int timeout_ms) {
    pthread_mutex_lock(&command_data.mutex);
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(&command_data.cond, &command_data.mutex, &timeout);
    int code = 0;
    if (command_data.data != NULL) {
        code = *(int*)command_data.data;
    }
    
    pthread_mutex_unlock(&command_data.mutex);
    return (result == 0) ? code : -1; // Return code if signaled, -1 if timeout
}

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

// ADDED: Re-flash firmware option for Node2
static const char *node2_menu_items[] = {
    "1. LED On",
    "2. LED Off",
    "3. Read Single",
    "4. Re-flash firmware",  // ADDED: New reflash option for Node2
    "0. Exit"
};

static int node1_menu_count = sizeof(node1_menu_items) / sizeof(node1_menu_items[0]);
static int node2_menu_count = sizeof(node2_menu_items) / sizeof(node2_menu_items[0]);

// ==================== UART THREAD ====================
void *uart_thread_func(void *arg) {
    uint16_t resp_len = 0;

    while (1) {
        pthread_mutex_lock(&command_mutex);
        int cmd = get_command_code();
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
            // MODIFIED: Use notification wait instead of constant polling
            int new_cmd = wait_for_command_change(1000); // Wait 1000ms for command
            if (new_cmd == -1) {
                continue; // Timeout, try again
            }
            cmd = new_cmd;
            
            // Check if there's a pending command
            pthread_mutex_lock(&command_mutex);
            if (!command_pending) continue;
            command_pending = 0;
            pthread_mutex_unlock(&command_mutex);
        }

        // UPDATED: Auto read command should not block UI
        int block_ui = (cmd == 1 || cmd == 2 || cmd == 3 || cmd == 6) || 
                       (local_node_type == NODE_TYPE_2 && cmd == 4);  // UPDATED: Node2 reflash is cmd 4
        
        // Auto read command doesn't block UI
        if (cmd == AUTO_READ_COMMAND) {
            block_ui = 0;
        }
        
        if (block_ui) {
            pthread_mutex_lock(&command_mutex);
            is_busy = 1;  // Block UI when handling these commands
            pthread_mutex_unlock(&command_mutex);
        }

        unsigned char *resp = NULL;
        int t1_val = 0, t2_val = 0, t3_val = 0;
        uint16_t adc_value = 0;  // For Node2 ADC readings
        uint8_t ret = 0;
        int is_auto_read = (cmd == AUTO_READ_COMMAND);

        // Handle commands based on selected_node_type
        if (local_node_type == NODE_TYPE_1) {
            // ADDED: Handle auto read for Node1
            if (is_auto_read) {
                // SILENT auto read: send status command
                write_command(CMD_SEND_STATUS);
                resp = Read_Response(100, &resp_len);
                if (resp && resp_len >= 12) {
                    // Parse 12 byte binary data (3 x u32 little-endian)
                    t1_val = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24);
                    t2_val = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
                    t3_val = resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24);
                    
                    // ONLY update MQTT data, no UI status update
                    set_mqtt_data_n1(t1_val, t2_val, t3_val);
                }
                // Skip UI status update for auto read
                goto cleanup;
            } else {
                // Normal manual commands
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
                        // Parse 12 byte binary data (3 x u32 little-endian)
                        if (resp && resp_len >= 12) {
                            // Convert little-endian bytes to u32 values
                            t1_val = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24);
                            t2_val = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
                            t3_val = resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24);
                        }
                        break;
                    case 7:
                        write_command(CMD_STOP_SYSTEM);
                        break;
                    case 8:
                        write_init(115200);
                        break;
                    case 9:  // CMD_REFLASH
                        pthread_mutex_lock(&command_mutex);
                        is_busy = 1;  // Block UI
                        snprintf(status_response, sizeof(status_response), "Flashing firmware...");
                        status_color = 4;
                        pthread_mutex_unlock(&command_mutex);
                        // Run shell command to flash
                        ret = system("bash -c 'source ../../../esptool-env/bin/activate && "
                                     "esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x10000 ../dcs-test.bin && "
                                     "deactivate'");
                        Clear_Startup_UART(uart_fd, 10000);  // Clear UART buffer after flashing
                        pthread_mutex_lock(&command_mutex);
                        is_busy = 0;
                        pthread_mutex_unlock(&command_mutex);
                        break;
                    default:
                        break;
                }
            }

            // ========== STATUS HANDLING FOR NODE1 (Motor Controller) - Only manual commands ==========
            pthread_mutex_lock(&command_mutex);
            if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
                snprintf(status_response, sizeof(status_response), "Node1 Command %d executed successfully", cmd);
                snprintf(receive_data, sizeof(receive_data), "OK");
                status_color = 2;
            } 
            else if (resp && resp_len >= 12 && cmd == 6) {  // Binary status data (12 bytes)
                snprintf(status_response, sizeof(status_response), "Node1 Status command executed");
                snprintf(receive_data, sizeof(receive_data), "T1:%d,T2:%d,T3:%d", t1_val, t2_val, t3_val);
                status_color = 2;
                set_mqtt_data_n1(t1_val, t2_val, t3_val);
            }
            else if (resp_len == 0 && (cmd == 4 || cmd == 5 || cmd == 7 || cmd == 8)) {  // Commands with no response
                snprintf(status_response, sizeof(status_response), "Node1 Command %d executed", cmd);
                snprintf(receive_data, sizeof(receive_data), "No response expected for Node1 command %d", cmd);
                status_color = 2;
            }
            else if (cmd == 9) {  // Re-flash firmware for Node1
                if (ret == 0) {
                    snprintf(status_response, sizeof(status_response), "Node1 firmware flashed successfully.");
                    snprintf(receive_data, sizeof(receive_data), "Node1 esptool executed successfully.");
                    status_color = 2;
                } else {
                    snprintf(status_response, sizeof(status_response), "Node1 flashing failed!");
                    snprintf(receive_data, sizeof(receive_data), "Node1 esptool error: return %d", ret);
                    status_color = 3;
                }
            }
            else {
                snprintf(status_response, sizeof(status_response), "Node1 Command %d failed", cmd);
                snprintf(receive_data, sizeof(receive_data), "Node1 no response or error for command %d", cmd);
                status_color = 3;
            }
            pthread_mutex_unlock(&command_mutex);
        }
        else if (local_node_type == NODE_TYPE_2) {
            // ADDED: Handle auto read for Node2
            if (is_auto_read) {
                // SILENT auto read: send read single command
                write_command(CMD2_READ_SINGLE);
                resp = Read_Response(2000, &resp_len);  // Expect 2 bytes
                if (resp && resp_len >= 2) {
                    // Parse 2 byte ADC data (u16 little-endian)
                    adc_value = resp[0] | (resp[1] << 8);  // Little-endian conversion
                    
                    // ONLY update MQTT data, no UI status update
                    set_mqtt_data_n2(adc_value);
                }
                // Skip UI status update for auto read
                goto cleanup;
            } else {
                // Normal manual commands
                switch (cmd) {
                    case 1:
                        write_command(CMD2_LED_ON);  // Assume defined in control_command.h
                        break;
                    case 2:
                        write_command(CMD2_LED_OFF);
                        break;
                    case 3:
                        write_command(CMD2_READ_SINGLE);
                        resp = Read_Response(2000, &resp_len);  // Expect 2 bytes
                        // Parse 2 byte ADC data (u16 little-endian)
                        if (resp && resp_len >= 2) {
                            adc_value = resp[0] | (resp[1] << 8);  // Little-endian conversion
                        }
                        break;
                    // ADDED: Reflash firmware case for Node2
                    case 4:  // ADDED: CMD_REFLASH for Node2
                        is_busy = 1;
                        pthread_mutex_lock(&command_mutex);
                        snprintf(status_response, sizeof(status_response), "Flashing Node2 firmware...");
                        status_color = 4;
                        pthread_mutex_unlock(&command_mutex);

                        // ADDED: Run shell command to flash Node2 firmware
                        ret = system("bash -c 'source ../../../esptool-env/bin/activate && "
                                     "esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x10000 ../hello1.bin && "
                                     "deactivate'");
                        Clear_Startup_UART(uart_fd, 10000);
                        is_busy = 0;
                        break;
                    default:
                        break;
                }
            }
            
            // ========== STATUS HANDLING FOR NODE2 (Sensor Board) - Only manual commands ==========
            pthread_mutex_lock(&command_mutex);
            if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
                snprintf(status_response, sizeof(status_response), "Node2 Command %d executed successfully", cmd);
                snprintf(receive_data, sizeof(receive_data), "OK");
                status_color = 2;
            }
            else if (resp && resp_len >= 2 && (cmd == 3)) {  // Single ADC read (2 bytes binary)
                snprintf(status_response, sizeof(status_response), "Node2 Single read command executed");
                snprintf(receive_data, sizeof(receive_data), "ADC: %d", adc_value);
                status_color = 2;
                set_mqtt_data_n2(adc_value);
            }
            else if (resp_len == 0 && (cmd == 1 || cmd == 2)) {  // LED commands with no response
                snprintf(status_response, sizeof(status_response), "Node2 LED command %d executed", cmd);
                snprintf(receive_data, sizeof(receive_data), "No response expected for Node2 LED command %d", cmd);
                status_color = 2;
            }
            else if (cmd == 4) {  // Re-flash firmware for Node2
                if (ret == 0) {
                    snprintf(status_response, sizeof(status_response), "Node2 firmware flashed successfully.");
                    snprintf(receive_data, sizeof(receive_data), "Node2 esptool executed successfully.");
                    status_color = 2;
                } else {
                    snprintf(status_response, sizeof(status_response), "Node2 flashing failed!");
                    snprintf(receive_data, sizeof(receive_data), "Node2 esptool error: return %d", ret);
                    status_color = 3;
                }
            }
            else {
                snprintf(status_response, sizeof(status_response), "Node2 Command %d failed", cmd);
                snprintf(receive_data, sizeof(receive_data), "Node2 no response or error for command %d", cmd);
                status_color = 3;
            }
            pthread_mutex_unlock(&command_mutex);
        }

cleanup:
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
    int command_code = -1; // Initialize command code

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

    // Use pthread mutex to safely set the shared node type (note the locking/unlocking)
    pthread_mutex_lock(&command_mutex);
    shared_node_type = selected_node_type;
    pthread_mutex_unlock(&command_mutex);

    // ADDED: Initialize auto read timer
    last_user_interaction = time(NULL);

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
        
        // ADDED: Display auto read status
        time_t current_time = time(NULL);
        int time_since_interaction = (int)(current_time - last_user_interaction);
        attron(COLOR_PAIR(5));
        mvprintw(4, 0, "Auto data collection: %s", 
                 time_since_interaction >= AUTO_READ_INTERVAL ? "ACTIVE" : "IDLE");
        attroff(COLOR_PAIR(5));

        for (int i = 0; i < num_items; i++) {
            if (i == highlight)
                attron(COLOR_PAIR(1));
            mvprintw(6 + i, 0, "%s", menu_items[i]);
            if (i == highlight)
                attroff(COLOR_PAIR(1));
        }
        
        pthread_mutex_lock(&command_mutex);
        attron(COLOR_PAIR(status_color));
        mvprintw(6 + num_items + 2, 0, "Status: %s", status_response);
        attroff(COLOR_PAIR(status_color));
        attron(COLOR_PAIR(5)); // Received Data Color
        mvprintw(6 + num_items + 3, 0, "Received Data: %s", receive_data);
        attroff(COLOR_PAIR(5));
        pthread_mutex_unlock(&command_mutex);

        refresh();

        key = getch();
        
        // ADDED: Check for auto read timeout
        current_time = time(NULL);
        if (key == ERR) { // No key pressed (timeout)
            if (current_time - last_user_interaction >= AUTO_READ_INTERVAL) {
                // SILENT auto read - only send command, no UI notification
                set_command_code(AUTO_READ_COMMAND);
                pthread_mutex_lock(&command_mutex);
                command_pending = 1;
                pthread_mutex_unlock(&command_mutex);
                last_user_interaction = current_time; // Reset timer
            }
            continue;
        }

        // ADDED: Update last interaction time on any key press
        last_user_interaction = current_time;

        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? num_items - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == num_items - 1) ? 0 : highlight + 1;
                break;
            case 10: // Enter
                if (is_busy) {
                    pthread_mutex_lock(&command_mutex);
                    snprintf(status_response, sizeof(status_response), "Busy: Please wait for command to finish");
                    status_color = 3;
                    pthread_mutex_unlock(&command_mutex);
                    break;
                }
                command_code = highlight + 1;
                set_command_code(command_code);
                pthread_mutex_lock(&command_mutex);
                command_pending = 1;
                pthread_mutex_unlock(&command_mutex);

                // UPDATED: Handle exit based on node with updated exit codes
                if ((selected_node_type == NODE_TYPE_1 && command_code == NODE1_EXIT_CODE) ||
                    (selected_node_type == NODE_TYPE_2 && command_code == NODE2_EXIT_CODE)) {
                    endwin();
                    printf("Exiting...\n");
                    close(uart_fd);
                    exit(0);
                }
                break;
        }

        usleep(50 * 1000); // Redraw delay
    }

    endwin();
    return NULL;
}
