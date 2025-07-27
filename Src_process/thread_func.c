#include "thread_func.h"
#include <time.h>
#include "node_config.h"

// ========== Shared state between threads ==========
volatile uint8_t is_busy = 0;

pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
char status_response[100] = "Waiting for command...";
int status_color = 2;
unsigned char receive_data[512] = {0};
unsigned char save_data[512] = {0};
int shared_node_type = 0;  // Initialize to 0 (no node selected yet)

shared_data_t command_data = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Helper functions for command_data operations:
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

// ==================== UART THREAD (Config-driven) ====================
void *uart_thread_func(void *arg) {
    uint16_t resp_len = 0;
    
    while (1) {
        pthread_mutex_lock(&command_mutex);
        int cmd = get_command_code();
        int pending = command_pending;
        command_pending = 0;
        int local_node_type = shared_node_type;
        pthread_mutex_unlock(&command_mutex);

        // Wait if node type not selected yet
        if (local_node_type == 0) {
            usleep(100 * 1000);
            continue;
        }

        if (!pending) {
            int new_cmd = wait_for_command_change(get_uart_wait_timeout());
            if (new_cmd == -1) {
                continue;
            }
            cmd = new_cmd;
            
            pthread_mutex_lock(&command_mutex);
            if (!command_pending) continue;
            command_pending = 0;
            pthread_mutex_unlock(&command_mutex);
        }

        // Get node config
        node_config_t *current_node = get_node_by_id(local_node_type);
        if (!current_node) {
            usleep(100 * 1000);
            continue;
        }

        // Find menu item for this command
        menu_item_t *menu_item = get_menu_item_by_cmd(current_node, cmd);
        int is_auto_read = (cmd == get_auto_read_command_id());
        
        // Determine if command should block UI
        int block_ui = 0;
        if (menu_item && menu_item->timeout_ms > 0) {
            block_ui = 1;
        }
        
        // Auto read doesn't block UI
        if (is_auto_read) {
            block_ui = 0;
        }

        if (block_ui) {
            pthread_mutex_lock(&command_mutex);
            is_busy = 1;
            pthread_mutex_unlock(&command_mutex);
        }

        unsigned char *resp = NULL;
        uint8_t ret = 0;

        // Handle auto read command
        if (is_auto_read) {
            // Send auto read command for current node
            menu_item_t *auto_read_item = get_menu_item_by_cmd(current_node, current_node->auto_read_cmd);
            if (auto_read_item && strlen(auto_read_item->hex_value) > 0) {
                // Execute auto read command using hex_value
                execute_uart_command(current_node, auto_read_item, &resp, &resp_len, 1); // silent=1
            }
            goto cleanup;
        }

        // Handle regular commands
        if (menu_item) {
            if (strlen(menu_item->hex_value) > 0) {
                // Regular UART command using hex_value
                execute_uart_command(current_node, menu_item, &resp, &resp_len, 0); // silent=0
            } else if (cmd == 9 || cmd == 4) {
                // Handle reflash command (empty hex_value indicates special command)
                pthread_mutex_lock(&command_mutex);
                is_busy = 1;
                snprintf(status_response, sizeof(status_response), "Flashing %s firmware...", current_node->name);
                status_color = 4;
                pthread_mutex_unlock(&command_mutex);

                // Execute reflash script from config
                ret = system(current_node->reflash_script);
                Clear_Startup_UART(uart_fd, get_uart_clear_timeout());

                pthread_mutex_lock(&command_mutex);
                if (ret == 0) {
                    snprintf(status_response, sizeof(status_response), "%s firmware flashed successfully.", current_node->name);
                    snprintf(receive_data, sizeof(receive_data), "%s esptool executed successfully.", current_node->name);
                    status_color = 2;
                } else {
                    snprintf(status_response, sizeof(status_response), "%s flashing failed!", current_node->name);
                    snprintf(receive_data, sizeof(receive_data), "%s esptool error: return %d", current_node->name, ret);
                    status_color = 3;
                }
                is_busy = 0;
                pthread_mutex_unlock(&command_mutex);
            }
        }

cleanup:
        if (resp) {
            free(resp);
            resp_len = 0;
        }
        
        if (block_ui) {
            pthread_mutex_lock(&command_mutex);
            is_busy = 0;
            pthread_mutex_unlock(&command_mutex);
        }
    }
    
    return NULL;
}

// Helper function to execute UART commands using hex from config
void execute_uart_command(node_config_t *node, menu_item_t *menu_item, unsigned char **resp, uint16_t *resp_len, int silent) {
    *resp = NULL;
    *resp_len = 0;
    
    // Get hex command from config and convert to integer
    uint32_t uart_cmd = hex_string_to_int(menu_item->hex_value);
    if (uart_cmd == 0) return;
    
    // Send command
    write_command((uint8_t)uart_cmd);
    
    // Read response if timeout > 0
    if (menu_item->timeout_ms > 0) {
        *resp = Read_Response(menu_item->timeout_ms, resp_len);
    }
    
    // Process response
    process_uart_response(node, menu_item->cmd, *resp, *resp_len, silent);
}

// Helper function to process UART response
void process_uart_response(node_config_t *node, int cmd, unsigned char *resp, uint16_t resp_len, int silent) {
    if (silent) {
        // Only update MQTT data for auto read, no UI update
        update_mqtt_data_from_response(node, resp, resp_len);
        return;
    }
    
    // Update UI status and MQTT data for manual commands
    pthread_mutex_lock(&command_mutex);
    
    if (resp && resp_len >= 2 && strncmp((char *)resp, "OK", 2) == 0) {
        snprintf(status_response, sizeof(status_response), "%s Command %d executed successfully", node->name, cmd);
        snprintf(receive_data, sizeof(receive_data), "OK");
        status_color = 2;
    }
    else if (resp && resp_len > 0) {
        snprintf(status_response, sizeof(status_response), "%s Command %d executed", node->name, cmd);
        format_response_data(node, resp, resp_len);
        status_color = 2;
        update_mqtt_data_from_response(node, resp, resp_len);
    }
    else if (resp_len == 0) {
        snprintf(status_response, sizeof(status_response), "%s Command %d executed", node->name, cmd);
        snprintf(receive_data, sizeof(receive_data), "No response expected for %s command %d", node->name, cmd);
        status_color = 2;
    }
    else {
        snprintf(status_response, sizeof(status_response), "%s Command %d failed", node->name, cmd);
        snprintf(receive_data, sizeof(receive_data), "%s no response or error for command %d", node->name, cmd);
        status_color = 3;
    }
    
    pthread_mutex_unlock(&command_mutex);
}

// Helper function to format response data for display
void format_response_data(node_config_t *node, unsigned char *resp, uint16_t resp_len) {
    if (strcmp(node->data_structure, "node1_data_t") == 0 && resp_len >= 12) {
        // Node1: 3 x u32 values
        int t1 = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24);
        int t2 = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
        int t3 = resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24);
        snprintf(receive_data, sizeof(receive_data), "T1:%d,T2:%d,T3:%d", t1, t2, t3);
    }
    else if (strcmp(node->data_structure, "node2_data_t") == 0 && resp_len >= 2) {
        // Node2: 1 x u16 value
        uint16_t adc = resp[0] | (resp[1] << 8);
        snprintf(receive_data, sizeof(receive_data), "ADC: %d", adc);
    }
    else {
        // Generic hex dump
        char hex_str[1024] = {0};
        for (int i = 0; i < resp_len && i < 100; i++) {
            sprintf(hex_str + strlen(hex_str), "%02X ", resp[i]);
        }
        snprintf(receive_data, sizeof(receive_data), "Raw: %s", hex_str);
    }
}

// Helper function to update MQTT data from response
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len) {
    if (strcmp(node->data_structure, "node1_data_t") == 0 && resp_len >= 12) {
        int t1 = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24);
        int t2 = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
        int t3 = resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24);
        set_mqtt_data_n1(t1, t2, t3);
    }
    else if (strcmp(node->data_structure, "node2_data_t") == 0 && resp_len >= 2) {
        uint16_t adc = resp[0] | (resp[1] << 8);
        set_mqtt_data_n2(adc);
    }
}

// ==================== UI THREAD (Config-driven) ====================
void *ui_thread_func(void *arg) {
    // Receive arguments from main: baudrate, device
    uint32_t baudrate = *(uint32_t *)((void **)arg)[0];
    char *device = (char *)((void **)arg)[1];
    
    // Config already loaded in main, no need to load again
    // Just verify it's loaded
    if (get_node_count() == 0) {
        printf("No nodes configuration found. Exiting UI thread.\n");
        return NULL;
    }
    
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
    timeout(100); // Non-blocking input

    // ========== Node selection variables ==========
    int selected_node_id = 0;
    node_config_t *selected_node = NULL;
    int node_highlight = 0;
    int node_key;
    bool node_selected = false;
    int command_code = -1;
    
    // ========== Auto read variables ==========
    volatile time_t last_user_interaction = 0;
    uint8_t auto_read_active = 0;
    
    // Initialize last interaction time
    last_user_interaction = time(NULL);

    while (1) {
        // ========== NODE SELECTION MENU ==========
        if (!node_selected) {
            clear();
            attron(COLOR_PAIR(4));
            mvprintw(0, 0, "===== Select Node Type =====");
            attroff(COLOR_PAIR(4));
            
            // Display nodes from config
            for (int i = 0; i < get_node_count(); i++) {
                node_config_t *node = get_node_by_index(i);
                if (i == node_highlight)
                    attron(COLOR_PAIR(1));
                mvprintw(2 + i, 0, "%d. %s", node->node_id, node->name);
                if (i == node_highlight)
                    attroff(COLOR_PAIR(1));
            }
            
            refresh();
            node_key = getch();
            
            switch (node_key) {
                case KEY_UP:
                    node_highlight = (node_highlight == 0) ? get_node_count() - 1 : node_highlight - 1;
                    break;
                case KEY_DOWN:
                    node_highlight = (node_highlight == get_node_count() - 1) ? 0 : node_highlight + 1;
                    break;
                case 10: // Enter
                    selected_node = get_node_by_index(node_highlight);
                    if (selected_node) {
                        selected_node_id = selected_node->node_id;
                        node_selected = true;
                        
                        // Set shared node type safely
                        pthread_mutex_lock(&command_mutex);
                        shared_node_type = selected_node_id;
                        pthread_mutex_unlock(&command_mutex);
                        
                        // Reset auto read timer
                        last_user_interaction = time(NULL);
                        
                        printf("Selected: %s\n", selected_node->name);
                    }
                    break;
                case 'q':
                case 'Q':
                    endwin();
                    printf("Exiting...\n");
                    exit(0);
                    break;
            }
            
            usleep(get_ui_refresh_delay()); // Use config value
            continue;
        }

        // ========== MAIN COMMAND MENU ==========
        clear();
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "=== COMMAND SELECTION MENU ===");
        attroff(COLOR_PAIR(4));
        
        mvprintw(1, 0, "Current Baudrate: %u", baudrate);
        mvprintw(2, 0, "Current Device: %s", device);
        mvprintw(3, 0, "Current Node: %s", selected_node->name);
        
        // Display auto read status
        time_t current_time = time(NULL);
        int time_since_interaction = (int)(current_time - last_user_interaction);
        
        if (time_since_interaction >= selected_node->auto_read_interval) {
            auto_read_active = 1;
        }
        
        attron(COLOR_PAIR(5));
        mvprintw(4, 0, "Auto data collection: %s (last: %ds ago)",
                auto_read_active ? "ACTIVE" : "IDLE",
                time_since_interaction);
        attroff(COLOR_PAIR(5));
        
        // Display menu items from config
        static int highlight = 0;
        for (int i = 0; i < selected_node->menu_count; i++) {
            if (i == highlight)
                attron(COLOR_PAIR(1));
            mvprintw(6 + i, 0, "%d. %s", 
                    selected_node->menu_items[i].cmd,
                    selected_node->menu_items[i].label);
            if (i == highlight)
                attroff(COLOR_PAIR(1));
        }
        
        // Display status
        pthread_mutex_lock(&command_mutex);
        attron(COLOR_PAIR(status_color));
        mvprintw(6 + selected_node->menu_count + 2, 0, "Status: %s", status_response);
        attroff(COLOR_PAIR(status_color));
        
        attron(COLOR_PAIR(5));
        mvprintw(6 + selected_node->menu_count + 3, 0, "Received Data: %s", receive_data);
        attroff(COLOR_PAIR(5));
        pthread_mutex_unlock(&command_mutex);
        
        refresh();
        
        int key = getch();
        
        // Check for auto read timeout
        current_time = time(NULL);
        if (key == ERR) { // No key pressed (timeout)
            if (current_time - last_user_interaction >= selected_node->auto_read_interval) {
                // Silent auto read
                set_command_code(get_auto_read_command_id());
                pthread_mutex_lock(&command_mutex);
                command_pending = 1;
                pthread_mutex_unlock(&command_mutex);
            }
            continue;
        }
        
        // Update last interaction time on any key press
        if (key != ERR) {
            last_user_interaction = current_time;
            auto_read_active = 0; // Reset to IDLE when user interacts
        }
        
        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? selected_node->menu_count - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == selected_node->menu_count - 1) ? 0 : highlight + 1;
                break;
            case 10: // Enter
                if (is_busy) {
                    pthread_mutex_lock(&command_mutex);
                    snprintf(status_response, sizeof(status_response), "Busy: Please wait for command to finish");
                    status_color = 3;
                    pthread_mutex_unlock(&command_mutex);
                    break;
                }
                
                // Get command from menu item
                if (highlight < selected_node->menu_count) {
                    command_code = selected_node->menu_items[highlight].cmd;
                    set_command_code(command_code);
                    
                    pthread_mutex_lock(&command_mutex);
                    command_pending = 1;
                    pthread_mutex_unlock(&command_mutex);
                    
                    // Handle exit command
                    if (command_code == 0) {
                        node_selected = false;
                        selected_node_id = 0;
                        selected_node = NULL;
                        shared_node_type = 0;
                        highlight = 0;
                        continue;
                    }
                }
                break;
            case 'b':
            case 'B':
                // Back to node selection
                node_selected = false;
                selected_node_id = 0;
                selected_node = NULL;
                shared_node_type = 0;
                highlight = 0;
                break;
            case 'q':
            case 'Q':
                endwin();
                printf("Exiting...\n");
                close(uart_fd);
                exit(0);
                break;
        }
        
        usleep(get_ui_refresh_delay()); // Use config value
    }
    
    endwin();
    return NULL;
}
