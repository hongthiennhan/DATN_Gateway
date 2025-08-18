#include "thread_func.h"
#include <ncurses.h>
#include "node_config.h"

// Shared state between threads
volatile uint8_t is_busy = 0;
pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
char status_response[100] = "System ready";
int status_color = 2;
unsigned char receive_data[512] = {0};
int shared_node_type = 0;

shared_data_t command_data = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// ==================== UART THREAD - Passive listening for node data ====================
void *uart_thread_func(void *arg) {
    #ifdef DEBUG
    printf("UART thread started - listening for automatic node data\n");
    #endif
    
    uint16_t data_len = 0;
    unsigned char *data_buffer = NULL;
    time_t last_11_second_detection = 0;  // Chỉ cần timer cho 11 giây detection
    
    while (1) {
        // Skip processing during config reload
        if (config_reloading) {
            usleep(1000 * 1000); // Wait 100ms during reload
            continue;
        }
        
        time_t current_time = time(NULL);
        
        // ============= NODE DETECTION EVERY 11 SECONDS =============
        if ((current_time - last_11_second_detection) >= 11) {
            #ifdef DEBUG
            printf("Starting 11-second node detection cycle\n");
            #endif
            
            pthread_mutex_lock(&config_mutex);
            if (!config_reloading) {
                for (int i = 0; i < get_node_count(); i++) {
                    node_config_t *node = get_node_by_index(i);
                    if (node && node->detection_commands && node->detection_count > 0) {
                        #ifdef DEBUG
                        printf("Detecting node %d (%s), write command 0x%02X expecting '%s'\n",
                               node->node_id, node->name, node->detection_commands[0].command, node->detection_commands[0].expected_response);
                        #endif
                        
                        // Send detection command from JSON
                        write_command(node->detection_commands[0].command);
                        // uint16_t bytes_written = write(uart_fd, &node->detection_commands[0].command, 1);
                        // if (bytes_written < 1) {
                        //     #ifdef DEBUG
                        //     printf("Failed to write detection command for node %d\n", node->node_id);
                        //     #endif
                        // }
                        // Read response with timeout from JSON
                        int timeout_ms = node->detection_commands[0].timeout_ms;
                        if (timeout_ms <= 0) timeout_ms = 1000; // Default 1 second
                        
                        data_buffer = Read_Response(timeout_ms, &data_len);
                        
                        if (data_buffer && data_len > 0) {
                            // Convert response to string for comparison
                            char response_str[512] = {0};
                            int max_len = (data_len < 511) ? data_len : 511;
                            memcpy(response_str, data_buffer, max_len);
                            response_str[max_len] = '\0';
                            
                            // Check if response contains expected_response
                            if (strstr(response_str, node->detection_commands[0].expected_response) != NULL) {
                                // Node detected successfully
                                node->detected = 1;
                                node->last_detection = current_time;
                                
                                #ifdef DEBUG
                                printf("Node %d DETECTED - received '%s'\n", 
                                       node->node_id, node->detection_commands[0].expected_response);
                                #endif
                            } else {
                                // Wrong response
                                node->detected = 0;
                                
                                #ifdef DEBUG
                                printf("Node %d NOT DETECTED - wrong response: '%s'\n", 
                                       node->node_id, response_str);
                                #endif
                            }
                            
                            free(data_buffer);
                            data_buffer = NULL;
                        } else {
                            // No response - không detect được
                            node->detected = 0;
                            
                            #ifdef DEBUG
                            printf("Node %d NOT DETECTED - no response\n", node->node_id);
                            #endif
                        }
                        
                        // Small delay between nodes
                        usleep(100 * 1000); // 100ms
                    }
                }
            }
            pthread_mutex_unlock(&config_mutex);
            
            last_11_second_detection = current_time;
            
            #ifdef DEBUG
            printf("11-second detection cycle completed\n");
            #endif
        }
        // =======================================================
        
        // Check for data from UART (normal operation)
        if (Check_UART_Data_Available()) {
            data_buffer = Read_Response(1000, &data_len);
            if (data_buffer && data_len > 0) {
                #ifdef DEBUG
                printf("Received automatic data from node: %d bytes\n", data_len);
                #endif
                
                // SAFE process received data
                safe_process_uart_data(data_buffer, data_len);
                
                // Update status
                pthread_mutex_lock(&command_mutex);
                snprintf(status_response, sizeof(status_response),
                         "Received data: %d bytes", data_len);
                status_color = 2;
                
                // Format for display
                char hex_str[256] = {0};
                int max_display = (data_len > 50) ? 50 : data_len;
                for (int i = 0; i < max_display; i++) {
                    sprintf(hex_str + strlen(hex_str), "%02X ", data_buffer[i]);
                }
                snprintf((char*)receive_data, sizeof(receive_data), "Auto Data[%d bytes]: %s%s",
                         data_len, hex_str, (data_len > 50) ? "..." : "");
                pthread_mutex_unlock(&command_mutex);
                
                free(data_buffer);
                data_buffer = NULL;
                data_len = 0;
            }
        }
        
        // Handle server control commands
        server_control_cmd_t control_cmd;
        if (get_control_command(&control_cmd) == 0) {
            pthread_mutex_lock(&config_mutex);
            if (!config_reloading) {
                node_config_t *node = get_node_by_id(control_cmd.node_id);
                if (node) {
                    menu_item_t *menu_item = get_menu_item_by_cmd(node, control_cmd.cmd_id);
                    if (menu_item) {
                        #ifdef DEBUG
                        printf("Executing server command: node=%d, cmd=%d\n",
                               control_cmd.node_id, control_cmd.cmd_id);
                        #endif
                        
                        uint32_t uart_cmd = hex_string_to_int(menu_item->hex_value);
                        if (uart_cmd != 0) {
                            write_command((uint8_t)uart_cmd);
                        }
                    }
                }
            }
            pthread_mutex_unlock(&config_mutex);
        }
        
        usleep(100 * 1000); // 100ms sleep
    }
    
    return NULL;
}

/**
 * Assign received data to appropriate node
 */
void safe_process_uart_data(unsigned char *data, uint16_t data_len) {
    if (!data || data_len == 0 || config_reloading) return;
    
    pthread_mutex_lock(&config_mutex);
    int node_count = get_node_count();
    for (int i = 0; i < node_count; i++) {
        node_config_t *node = get_node_by_index(i);
        if (node && node->detected) {
            // Update last data received timestamp
            node->last_data_received = time(NULL);
            // Update MQTT data with received response
            update_mqtt_data_from_response(node, data, data_len);
            #ifdef DEBUG
            printf("Data assigned to node %d (%s)\n", node->node_id, node->name);
            #endif
            break;
        }
    }
    pthread_mutex_unlock(&config_mutex);
}

/**
 * Process UART response from node (legacy function, still used by server commands)
 */
void process_uart_response(node_config_t *node, int cmd, unsigned char *resp, uint16_t resp_len, int silent) {
    if (silent) {
        // Only update MQTT data, no UI update
        update_mqtt_data_from_response(node, resp, resp_len);
        return;
    }
    
    pthread_mutex_lock(&command_mutex);
    
    if (resp && resp_len > 0) {
        snprintf(status_response, sizeof(status_response),
                 "Node %s: Command %d executed", node->name, cmd);
        
        // Format response for display
        char hex_str[256] = {0};
        int max_display = (resp_len > 50) ? 50 : resp_len;
        
        for (int i = 0; i < max_display; i++) {
            sprintf(hex_str + strlen(hex_str), "%02X ", resp[i]);
        }
        
        snprintf((char*)receive_data, sizeof(receive_data), "Data[%d bytes]: %s%s",
                 resp_len, hex_str, (resp_len > 50) ? "..." : "");
        status_color = 2;
        
        update_mqtt_data_from_response(node, resp, resp_len);
    } else {
        snprintf(status_response, sizeof(status_response),
                 "Node %s: Command %d - no response", node->name, cmd);
        snprintf((char*)receive_data, sizeof(receive_data), "No data received");
        status_color = 3;
    }
    
    pthread_mutex_unlock(&command_mutex);
}

/**
 * Update MQTT data with received response
 */
void update_mqtt_data_from_response(node_config_t *node, unsigned char *resp, uint16_t resp_len) {
    if (!node || !node->mqtt_data || !resp || resp_len == 0) return;
    
    pthread_mutex_lock(&node->mqtt_data->mutex);
    
    // Free old data if exists
    if (node->mqtt_data->data) {
        raw_data_t *old_data = (raw_data_t*)node->mqtt_data->data;
        if (old_data->data) {
            free(old_data->data);
        }
        free(old_data);
    }
    
    // Store new raw data
    raw_data_t *raw_data = malloc(sizeof(raw_data_t));
    if (raw_data) {
        raw_data->length = resp_len;
        raw_data->timestamp = time(NULL); // ADD timestamp
        raw_data->data = malloc(resp_len);
        if (raw_data->data) {
            memcpy(raw_data->data, resp, resp_len);
            node->mqtt_data->data = raw_data;
            pthread_cond_signal(&node->mqtt_data->cond);
        } else {
            free(raw_data);
        }
    }
    
    pthread_mutex_unlock(&node->mqtt_data->mutex);
}

// ==================== UI THREAD - Enhanced config menu ====================
void *ui_thread_func(void *arg) {
    uint32_t baudrate = *(uint32_t *)((void **)arg)[0];
    char *device = (char *)((void **)arg)[1];
    
    // Initialize ncurses
    initscr();
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_WHITE); // Highlight
    init_pair(2, COLOR_GREEN, COLOR_BLACK); // Success
    init_pair(3, COLOR_RED, COLOR_BLACK);   // Error
    init_pair(4, COLOR_YELLOW, COLOR_BLACK); // Warning
    init_pair(5, COLOR_CYAN, COLOR_BLACK);   // Info
    
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    timeout(100); // timeout for refresh

    int highlight = 0;
    int key_check;
    while (1) {
        clear();
        
        // Header
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "===== IoT Gateway Configuration Menu =====");
        attroff(COLOR_PAIR(4));
        
        // System info
        mvprintw(2, 0, "UART: %s @ %u baud", device, baudrate);
        mvprintw(3, 0, "Nodes detected: %d", safe_get_node_count());
        mvprintw(4, 0, "Communication: %s", get_communication_type_name(get_communication_type())); // ADD communication type
        
        pthread_mutex_lock(&command_mutex);
        attron(COLOR_PAIR(status_color));
        mvprintw(5, 0, "Status: %s", status_response); // Move down due to new line
        attroff(COLOR_PAIR(status_color));
        
        if (strlen((char*)receive_data) > 0) {
            attron(COLOR_PAIR(5));
            mvprintw(6, 0, "Last data: %s", receive_data); // Move down due to new line
            attroff(COLOR_PAIR(5));
        }
        pthread_mutex_unlock(&command_mutex);
        
        // Menu options - ADD new communication options
        const char *menu_options[] = {
            "View System Status",
            "View Communication Config", 
            "Select Communication Type",  // NEW
            "Reload Configuration",
            "View Node Configuration",
            "Exit"
        };
        
        int num_options = sizeof(menu_options) / sizeof(menu_options[0]);
        mvprintw(8, 0, "Configuration Options:"); // Move down due to new lines
        
        for (int i = 0; i < num_options; i++) {
            if (i == highlight) {
                attron(COLOR_PAIR(1));
            }
            mvprintw(10 + i, 2, "%d. %s", i + 1, menu_options[i]); // Move down due to new lines
            if (i == highlight) {
                attroff(COLOR_PAIR(1));
            }
        }
        
        // Instructions
        attron(COLOR_PAIR(5));
        mvprintw(10 + num_options + 2, 0, "Use UP/DOWN arrows and ENTER to select");
        mvprintw(10 + num_options + 3, 0, "Press 'q' to quit, 'r' to refresh");
        attroff(COLOR_PAIR(5));
        
        refresh();
        
        int key = getch();
        switch (key) {
            case KEY_UP:
                highlight = (highlight == 0) ? num_options - 1 : highlight - 1;
                break;
            case KEY_DOWN:
                highlight = (highlight == num_options - 1) ? 0 : highlight + 1;
                break;
            case 10: // ENTER
                switch (highlight) {
                    case 0: // View System Status
                        clear();
                        system_info_t *sys_info = get_system_info();
                        if (sys_info) {
                            mvprintw(1, 0, "=== System Status ===");
                            mvprintw(3, 0, "Firmware: %s", sys_info->firmware_version);
                            mvprintw(4, 0, "Device Type: %s", sys_info->device_type);
                            mvprintw(5, 0, "Manufacturer: %s", sys_info->manufacturer);
                            mvprintw(6, 0, "Model: %s", sys_info->model);
                            mvprintw(8, 0, "UART Device: %s", device);
                            mvprintw(9, 0, "Baudrate: %u", baudrate);
                            mvprintw(10, 0, "Detected Nodes: %d", safe_get_node_count());
                            mvprintw(11, 0, "Communication Type: %s", get_communication_type_name(get_communication_type())); // ADD
                        }
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        key_check = getch();
                        while (key_check == ERR) {
                            key_check = getch(); // Wait for any key
                        }
                        break;
                        
                    case 1: // View Communication Config - UPDATED
                        clear();
                        communication_type_t comm_type = get_communication_type();
                        mvprintw(1, 0, "=== Communication Configuration ===");
                        mvprintw(3, 0, "Current Type: %s", get_communication_type_name(comm_type));
                        
                        if (comm_type == COMM_TYPE_MQTT) {
                            mqtt_config_t *mqtt_cfg = get_mqtt_config();
                            if (mqtt_cfg) {
                                mvprintw(5, 0, "MQTT Configuration:");
                                mvprintw(6, 0, "  Broker: %s:%d", mqtt_cfg->broker_host, mqtt_cfg->broker_port);
                                mvprintw(7, 0, "  Client ID: %s", mqtt_cfg->client_id);
                                mvprintw(8, 0, "  Username: %s", mqtt_cfg->username);
                                mvprintw(9, 0, "  Topic Telemetry: %s", mqtt_cfg->topic_telemetry);
                                mvprintw(10, 0, "  Topic Control: %s", mqtt_cfg->topic_control);
                                mvprintw(11, 0, "  QoS: %d", mqtt_cfg->qos);
                                mvprintw(12, 0, "  Publish Interval: %d sec", mqtt_cfg->publish_interval);
                            }
                        } else {
                            mvprintw(5, 0, "Configuration: Not implemented yet");
                        }
                        
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        key_check = getch();
                        while (key_check == ERR) {
                            key_check = getch(); // Wait for any key
                        }
                        break;
                        
                    case 2: // NEW: Select Communication Type
                        clear();
                        mvprintw(1, 0, "=== Select Communication Type ===");
                        mvprintw(3, 0, "Available Communication Types:");
                        
                        const char *comm_types[] = {
                            "MQTT",
                            "HTTP (Coming soon)",
                            "WebSocket (Coming soon)",
                            "TCP (Coming soon)"
                        };
                        
                        int comm_highlight = (int)get_communication_type();
                        int comm_selecting = 1;
                        
                        while (comm_selecting) {
                            for (int i = 0; i < COMM_TYPE_COUNT; i++) {
                                if (i == comm_highlight) {
                                    attron(COLOR_PAIR(1));
                                }
                                if (i == 0) {
                                    mvprintw(5 + i, 2, "%d. %s %s", i + 1, comm_types[i],
                                            (i == (int)get_communication_type()) ? "[CURRENT]" : "");
                                } else {
                                    attron(COLOR_PAIR(3)); // Red for not implemented
                                    mvprintw(5 + i, 2, "%d. %s", i + 1, comm_types[i]);
                                    attroff(COLOR_PAIR(3));
                                }
                                if (i == comm_highlight) {
                                    attroff(COLOR_PAIR(1));
                                }
                            }
                            
                            mvprintw(5 + COMM_TYPE_COUNT + 2, 0, "Use UP/DOWN to select, ENTER to confirm, ESC to cancel");
                            refresh();
                            
                            int comm_key = getch();
                            switch (comm_key) {
                                case KEY_UP:
                                    comm_highlight = (comm_highlight == 0) ? COMM_TYPE_COUNT - 1 : comm_highlight - 1;
                                    break;
                                case KEY_DOWN:
                                    comm_highlight = (comm_highlight == COMM_TYPE_COUNT - 1) ? 0 : comm_highlight + 1;
                                    break;
                                case 10: // ENTER
                                    if (comm_highlight == 0) { // Only MQTT implemented
                                        set_communication_type((communication_type_t)comm_highlight);
                                        mvprintw(5 + COMM_TYPE_COUNT + 4, 0, "Communication type changed to: %s", 
                                                get_communication_type_name((communication_type_t)comm_highlight));
                                        refresh();
                                        sleep(1);
                                    } else {
                                        mvprintw(5 + COMM_TYPE_COUNT + 4, 0, "This type is not implemented yet!");
                                        refresh();
                                        sleep(1);
                                    }
                                    comm_selecting = 0;
                                    break;
                                case 27: // ESC
                                    comm_selecting = 0;
                                    break;
                            }
                            
                            // Clear selection lines
                            for (int i = 5; i < 5 + COMM_TYPE_COUNT + 6; i++) {
                                move(i, 0);
                                clrtoeol();
                            }
                        }
                        break;
                        
                    case 3: // Reload Configuration
                        clear();
                        mvprintw(1, 0, "Reloading configuration...");
                        refresh();
                        
                        int load_result = safe_reload_config();
                        
                        if (load_result == 0) {
                            attron(COLOR_PAIR(2));
                            mvprintw(3, 0, "Configuration reloaded successfully!");
                            mvprintw(4, 0, "Loaded %d nodes", safe_get_node_count());
                            attroff(COLOR_PAIR(2));
                        } else {
                            attron(COLOR_PAIR(3));
                            mvprintw(3, 0, "Failed to reload configuration!");
                            attroff(COLOR_PAIR(3));
                        }
                        
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        
                        nodelay(stdscr, FALSE);
                        key_check = getch();
                        nodelay(stdscr, TRUE);
                        break;
                        
                    case 4: // View Node Configuration - UPDATED with last data time
                        clear();
                        mvprintw(1, 0, "=== Node Configuration ===");
                        int node_count = get_node_count();
                        if (node_count > 0) {
                            for (int i = 0; i < node_count; i++) {
                                node_config_t *node = get_node_by_index(i);
                                if (node) {
                                    attron(node->detected ? COLOR_PAIR(2) : COLOR_PAIR(3));
                                    mvprintw(3 + i * 2, 0, "Node %d: %s (%s) - %s",
                                             node->node_id, node->name, node->type,
                                             node->detected ? "DETECTED" : "NOT DETECTED");
                                    
                                    // ADD: Show last data received time
                                    if (node->detected && node->last_data_received > 0) {
                                        time_t now = time(NULL);
                                        int sec_ago = (int)(now - node->last_data_received);
                                        mvprintw(4 + i * 2, 2, "Last data: %d seconds ago", sec_ago);
                                    }
                                    attroff(node->detected ? COLOR_PAIR(2) : COLOR_PAIR(3));
                                }
                            }
                        } else {
                            mvprintw(3, 0, "No nodes configured");
                        }
                        
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        key_check = getch();
                        while (key_check == ERR) {
                            key_check = getch(); // Wait for any key
                        }
                        break;
                        
                    case 5: // Exit
                        endwin();
                        exit(0);
                        break;
                }
                break;
                
            case 'q':
            case 'Q':
                endwin();
                exit(0);
                break;
                
            case 'r':
            case 'R':
                // Refresh - do nothing, will update on next loop
                break;
        }
    }
    
    endwin();
    return NULL;
}
