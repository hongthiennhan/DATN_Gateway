#include "thread_func.h"
#include <time.h>
#include <string.h>  // Add missing include

// ========== Shared state between threads ==========
volatile uint8_t is_busy = 0;

pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
char status_response[100] = "Waiting for command...";
int status_color = 2;
unsigned char receive_data[512] = {0};
unsigned char save_data[512] = {0};
int shared_node_type = 0;

#define AUTO_READ_COMMAND 100
volatile time_t last_user_interaction = 0;
#define AUTO_READ_INTERVAL 1

shared_data_t command_data = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Helper functions (unchanged)
void set_command_code(int new_code) {
    pthread_mutex_lock(&command_data.mutex);
    if (command_data.data == NULL) {
        command_data.data = malloc(sizeof(int));
    }
    *(int*)command_data.data = new_code;
    pthread_cond_signal(&command_data.cond);
    pthread_mutex_unlock(&command_data.mutex);
}

int get_command_code() {
    pthread_mutex_lock(&command_data.mutex);
    int code = 0;
    if (command_data.data != NULL) {
        code = *(int*)command_data.data;
    }
    pthread_mutex_unlock(&command_data.mutex);
    return code;
}

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
    return (result == 0) ? code : -1;
}

// Menu definitions (unchanged)
static const char *node1_menu_items[] = {
    "1. Direction1", "2. Direction2", "3. Direction3", "4. Led_On", "5. Led_Off",
    "6. Send_Status", "7. Stop_System", "8. Init", "9. Re-flash firmware", "0. Exit program"
};

static const char *node2_menu_items[] = {
    "1. LED On", "2. LED Off", "3. Read Single", "4. Re-flash firmware", "0. Exit"
};

static int node1_menu_count = sizeof(node1_menu_items) / sizeof(node1_menu_items[0]);
static int node2_menu_count = sizeof(node2_menu_items) / sizeof(node2_menu_items[0]);

// Display raw data as hex string
void format_raw_data_display(unsigned char *data, int length, char *display_buffer, int buffer_size) {
    if (!data || length == 0) {
        strncpy(display_buffer, "No data", buffer_size - 1);
        display_buffer[buffer_size - 1] = '\0';
        return;
    }
    
    char hex_str[512];
    int max_display_bytes = (buffer_size - 50) / 3;
    int display_length = (length > max_display_bytes) ? max_display_bytes : length;
    
    hex_str[0] = '\0';
    for (int i = 0; i < display_length; i++) {
        sprintf(hex_str + strlen(hex_str), "%02X ", data[i]);
    }
    
    if (length > max_display_bytes) {
        snprintf(display_buffer, buffer_size, "Raw[%d bytes]: %s...", length, hex_str);
    } else {
        snprintf(display_buffer, buffer_size, "Raw[%d bytes]: %s", length, hex_str);
    }
}

// ==================== FIXED UART THREAD (unchanged) ====================
void *uart_thread_func(void *arg) {
    uint16_t resp_len = 0;

    while (1) {
        // FIXED: Better command handling logic
        int cmd = 0;
        int pending = 0;
        int local_node_type = 0;
        
        // Get current state safely
        pthread_mutex_lock(&command_mutex);
        cmd = get_command_code();
        pending = command_pending;
        local_node_type = shared_node_type;
        pthread_mutex_unlock(&command_mutex);

        // Wait if node type not selected yet
        if (local_node_type == 0) {
            usleep(100 * 1000);
            continue;
        }

        // FIXED: Proper command waiting logic
        if (!pending) {
            int new_cmd = wait_for_command_change(1000);
            if (new_cmd == -1) {
                continue; // Timeout, try again
            }
            cmd = new_cmd;
            
            // FIXED: Check and clear pending flag safely
            pthread_mutex_lock(&command_mutex);
            if (!command_pending) {
                pthread_mutex_unlock(&command_mutex); // ← FIX: Unlock before continue
                continue;
            }
            command_pending = 0;
            pthread_mutex_unlock(&command_mutex);
        } else {
            // FIXED: Clear pending flag for existing commands
            pthread_mutex_lock(&command_mutex);
            command_pending = 0;
            pthread_mutex_unlock(&command_mutex);
        }

        // Determine if UI should be blocked
        int block_ui = (cmd == 1 || cmd == 2 || cmd == 3 || cmd == 6) || 
                       (local_node_type == NODE_TYPE_2 && cmd == 4);
        
        if (cmd == AUTO_READ_COMMAND) {
            block_ui = 0;
        }
        
        // FIXED: Set busy flag safely
        if (block_ui) {
            pthread_mutex_lock(&command_mutex);
            is_busy = 1;
            pthread_mutex_unlock(&command_mutex);
        }

        unsigned char *resp = NULL;
        uint8_t ret = 0;
        int is_auto_read = (cmd == AUTO_READ_COMMAND);

        // FIXED: Handle commands for raw data storage
        if (local_node_type == NODE_TYPE_1) {
            if (is_auto_read) {
                // Auto read: send status command and store raw data
                write_command(CMD_SEND_STATUS);
                resp = Read_Response(100, &resp_len);
                if (resp && resp_len >= 12) {
                    set_mqtt_data_n1_raw(resp, resp_len);
                }
                goto cleanup;
            } else {
                // Manual commands
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
                        if (resp && resp_len >= 12) {
                            set_mqtt_data_n1_raw(resp, resp_len);
                        }
                        break;
                    case 7:
                        write_command(CMD_STOP_SYSTEM);
                        break;
                    case 8:
                        write_init(115200);
                        break;
                    case 9:  // Re-flash firmware
                        pthread_mutex_lock(&command_mutex);
                        is_busy = 1;
                        snprintf(status_response, sizeof(status_response), "Flashing firmware...");
                        status_color = 4;
                        pthread_mutex_unlock(&command_mutex);
                        
                        ret = system("bash -c 'source ../../../esptool-env/bin/activate && "
                                     "esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x10000 ../dcs-test.bin && "
                                     "deactivate'");
                        Clear_Startup_UART(uart_fd, 10000);
                        
                        pthread_mutex_lock(&command_mutex);
                        is_busy = 0;
                        pthread_mutex_unlock(&command_mutex);
                        break;
                    default:
                        break;
                }
            }

            // FIXED: Status handling for Node1 - avoid double locking
            if (!is_auto_read) {
                pthread_mutex_lock(&command_mutex);
                if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
                    snprintf(status_response, sizeof(status_response), "Node1 Command %d executed successfully", cmd);
                    snprintf(receive_data, sizeof(receive_data), "OK");
                    status_color = 2;
                } 
                else if (resp && resp_len >= 12 && cmd == 6) {
                    snprintf(status_response, sizeof(status_response), "Node1 Status command executed");
                    format_raw_data_display(resp, resp_len, receive_data, sizeof(receive_data));
                    status_color = 2;
                }
                else if (resp_len == 0 && (cmd == 4 || cmd == 5 || cmd == 7 || cmd == 8)) {
                    snprintf(status_response, sizeof(status_response), "Node1 Command %d executed", cmd);
                    snprintf(receive_data, sizeof(receive_data), "No response expected for Node1 command %d", cmd);
                    status_color = 2;
                }
                else if (cmd == 9) {
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
        }
        else if (local_node_type == NODE_TYPE_2) {
            if (is_auto_read) {
                // Auto read: send read single command and store raw data
                write_command(CMD2_READ_SINGLE);
                resp = Read_Response(2000, &resp_len);
                if (resp && resp_len >= 2) {
                    set_mqtt_data_n2_raw(resp, resp_len);
                }
                goto cleanup;
            } else {
                // Manual commands
                switch (cmd) {
                    case 1:
                        write_command(CMD2_LED_ON);
                        break;
                    case 2:
                        write_command(CMD2_LED_OFF);
                        break;
                    case 3:
                        write_command(CMD2_READ_SINGLE);
                        resp = Read_Response(2000, &resp_len);
                        if (resp && resp_len >= 2) {
                            set_mqtt_data_n2_raw(resp, resp_len);
                        }
                        break;
                    case 4:  // Re-flash firmware
                        pthread_mutex_lock(&command_mutex);
                        is_busy = 1;
                        snprintf(status_response, sizeof(status_response), "Flashing Node2 firmware...");
                        status_color = 4;
                        pthread_mutex_unlock(&command_mutex);

                        ret = system("bash -c 'source ../../../esptool-env/bin/activate && "
                                     "esptool --chip esp32 --port /dev/ttyUSB0 write-flash 0x10000 ../hello1.bin && "
                                     "deactivate'");
                        Clear_Startup_UART(uart_fd, 10000);
                        
                        pthread_mutex_lock(&command_mutex);
                        is_busy = 0;
                        pthread_mutex_unlock(&command_mutex);
                        break;
                    default:
                        break;
                }
            }
            
            // FIXED: Status handling for Node2 - avoid double locking
            if (!is_auto_read) {
                pthread_mutex_lock(&command_mutex);
                if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
                    snprintf(status_response, sizeof(status_response), "Node2 Command %d executed successfully", cmd);
                    snprintf(receive_data, sizeof(receive_data), "OK");
                    status_color = 2;
                }
                else if (resp && resp_len >= 2 && (cmd == 3)) {
                    snprintf(status_response, sizeof(status_response), "Node2 Single read command executed");
                    format_raw_data_display(resp, resp_len, receive_data, sizeof(receive_data));
                    status_color = 2;
                }
                else if (resp_len == 0 && (cmd == 1 || cmd == 2)) {
                    snprintf(status_response, sizeof(status_response), "Node2 LED command %d executed", cmd);
                    snprintf(receive_data, sizeof(receive_data), "No response expected for Node2 LED command %d", cmd);
                    status_color = 2;
                }
                else if (cmd == 4) {
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
        }

cleanup:
        if (resp) {
            free(resp);
            resp_len = 0;
        }

        // FIXED: Clear busy flag safely
        if (block_ui) {
            pthread_mutex_lock(&command_mutex);
            is_busy = 0;
            pthread_mutex_unlock(&command_mutex);
        }
    }

    return NULL;
}

// ==================== FIXED UI THREAD với Busy Timer Pause ====================
void *ui_thread_func(void *arg) {
    uint32_t baudrate = *(uint32_t *)((void **)arg)[0];
    char *device = (char *)((void **)arg)[1];

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
    timeout(100);

    int selected_node_type = 0;
    int node_highlight = 0;
    const char *node_menu[] = {
        "1. Node1 - Motor Controller",
        "2. Node2 - Sensor Board"
    };
    int node_menu_count = sizeof(node_menu) / sizeof(node_menu[0]);
    int node_key;
    bool node_selected = false;
    int command_code = -1;
    
    // ========== ADDED: Busy state tracking variables ==========
    volatile time_t busy_start_time = 0;  // Track when busy started
    uint8_t auto_read_active = 0;
    uint8_t was_busy = 0;  // Track previous busy state

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
                    attron(COLOR_PAIR(3));
                    mvprintw(5, 0, "Invalid selection. Press any key to try again.");
                    attroff(COLOR_PAIR(3));
                    refresh();
                    getch();
                }
                break;
        }
        usleep(50 * 1000);
    }

    const char **menu_items = NULL;
    int num_items = 0;
    if (selected_node_type == NODE_TYPE_1) {
        menu_items = node1_menu_items;
        num_items = node1_menu_count;
    } else if (selected_node_type == NODE_TYPE_2) {
        menu_items = node2_menu_items;
        num_items = node2_menu_count;
    }

    pthread_mutex_lock(&command_mutex);
    shared_node_type = selected_node_type;
    pthread_mutex_unlock(&command_mutex);

    last_user_interaction = time(NULL);

    int highlight = 0;
    int key;

    while (1) {
        clear();
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== COMMAND SELECTION MENU ===");
        attroff(COLOR_PAIR(4));

        mvprintw(1, 0, "Current Baudrate: %u", baudrate);
        mvprintw(2, 0, "Current Device: %s", device);
        mvprintw(3, 0, "Current Node: %d", selected_node_type);
        
        time_t current_time = time(NULL);
        
        // ========== FIXED: Busy state tracking và timer pause ==========
        pthread_mutex_lock(&command_mutex);
        uint8_t current_busy = is_busy;
        pthread_mutex_unlock(&command_mutex);
        
        // ADDED: Handle busy state transitions
        if (current_busy && !was_busy) {
            // Just became busy - record start time
            busy_start_time = current_time;
            auto_read_active = 0;  // Force IDLE when busy
        } else if (!current_busy && was_busy) {
            // Just finished being busy - extend interaction time
            time_t busy_duration = current_time - busy_start_time;
            last_user_interaction += busy_duration;  // Add busy time to interaction timer
            auto_read_active = 0;  // Reset to IDLE
        }
        
        was_busy = current_busy;  // Update previous state
        
        // FIXED: Only calculate time and check auto-read when NOT busy
        int time_since_interaction = 0;
        if (!current_busy) {
            time_since_interaction = (int)(current_time - last_user_interaction);
            
            // Only activate auto-read when not busy and interval passed
            if (time_since_interaction >= AUTO_READ_INTERVAL) {
                auto_read_active = 1;
            }
        } else {
            // When busy, show time as 0 and force IDLE
            time_since_interaction = 0;
            auto_read_active = 0;
        }
        
        // UPDATED: Display auto-read status with busy indication
        attron(COLOR_PAIR(5));
        if (current_busy) {
            mvprintw(4, 0, "Auto data collection: IDLE (SYSTEM BUSY - timer paused)");
        } else {
            mvprintw(4, 0, "Auto data collection: %s (last: %ds ago)", 
                     auto_read_active ? "ACTIVE" : "IDLE", 
                     time_since_interaction);
        }
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
        attron(COLOR_PAIR(5));
        mvprintw(6 + num_items + 3, 0, "Received Data: %s", receive_data);
        attroff(COLOR_PAIR(5));
        pthread_mutex_unlock(&command_mutex);

        refresh();

        key = getch();
        
        current_time = time(NULL);
        if (key == ERR) {
            // FIXED: Only trigger auto-read when NOT busy AND active
            if (!current_busy && auto_read_active) {
                set_command_code(AUTO_READ_COMMAND);
                pthread_mutex_lock(&command_mutex);
                command_pending = 1;
                pthread_mutex_unlock(&command_mutex);
            }
            continue;
        }
        else if (key != ERR) {
            last_user_interaction = current_time;
            auto_read_active = 0;  // Reset auto-read on user interaction
        }

        last_user_interaction = current_time;

        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? num_items - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == num_items - 1) ? 0 : highlight + 1;
                break;
            case 10: // Enter
                // FIXED: Check busy state safely
                pthread_mutex_lock(&command_mutex);
                uint8_t busy_state = is_busy;
                pthread_mutex_unlock(&command_mutex);
                
                if (busy_state) {
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

                if ((selected_node_type == NODE_TYPE_1 && command_code == NODE1_EXIT_CODE) ||
                    (selected_node_type == NODE_TYPE_2 && command_code == NODE2_EXIT_CODE)) {
                    endwin();
                    printf("Exiting...\n");
                    close(uart_fd);
                    exit(0);
                }
                break;
        }

        usleep(50 * 1000);
    }

    endwin();
    return NULL;
}
