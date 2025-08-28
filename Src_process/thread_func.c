#include "thread_func.h"
// Shared state between threads
volatile uint8_t is_busy = 0;
pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
char status_response[256] = "System ready";
int status_color = 2;
unsigned char receive_data[512] = {0};
unsigned char send_data[512] = {0};
int shared_node_type = 0;

shared_data_t command_data = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};
thread_pause_t uart_pause = {
    .is_paused = false, 
    .mutex = PTHREAD_MUTEX_INITIALIZER, 
    .cond = PTHREAD_COND_INITIALIZER
};

thread_pause_t modbus_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

thread_pause_t mqtt_pause = {
    .is_paused = false,
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
    time_t last_detection = 0;  // Chỉ cần timer cho 11 giây detection
    char response_str[128] = {0};
    char hex_str[256] = {0};
    while (1) {
        pthread_mutex_lock(&uart_pause.mutex);
        while (uart_pause.is_paused) {
            pthread_cond_wait(&uart_pause.cond, &uart_pause.mutex);  // Sleep and wait for signal
        }
        pthread_mutex_unlock(&uart_pause.mutex);
        // Skip processing during config reload
        if (config_reloading) {
            usleep(100 * 1000); // Wait 100ms during reload
            continue;
        }
        
        time_t current_time = time(NULL);
        
        // ============= NODE DETECTION EVERY 11 SECONDS =============
        if ((current_time - last_detection) >= 11) {
            #ifdef DEBUG
            printf("Starting 11-second node detection cycle\n");
            #endif
            
            pthread_mutex_lock(&config_mutex);
            if (!config_reloading) {
                for (int i = 0; i < get_node_count(); i++) {
                    node_config_t *node = get_node_by_index(i);
                    if (node && node->detection_commands && node->detection_count > 0 && strncmp(node->com_type, "UART", 4) == 0) {
                        uint8_t detect_cmd = hex_string_to_uint8(node->detection_commands[0].command);
                        #ifdef DEBUG
                        printf("Detecting node %d (%s), write command 0x%02X expecting '%s'\n",
                               node->node_id, node->name, detect_cmd, node->detection_commands[0].expected_response);
                        #endif
                        
                        // Send detection command from JSON
                        UART_Write_Command(detect_cmd);

                        // Read response with timeout from JSON
                        int timeout_ms = node->detection_commands[0].timeout_ms;
                        if (timeout_ms <= 0) timeout_ms = 1000; // Default 1 second
                        
                        data_buffer = UART_Read_Response(timeout_ms, &data_len);
                        
                        if (data_buffer && data_len > 0) {
                            // Convert response to string for comparison
                            int max_len = (data_len < 127) ? data_len : 127;
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
                            memset(response_str, 0, sizeof(response_str));
                            free(data_buffer);
                            data_buffer = NULL;
                        } else {
                            // No response - không detect được
                            node->detected = 0;
                            
                            #ifdef DEBUG
                            printf("Node %d NOT DETECTED - no response\n", node->node_id);
                            #endif
                        }
                    }
                }
            }
            pthread_mutex_unlock(&config_mutex);
            
            last_detection = current_time;
            
            #ifdef DEBUG
            printf("11-second detection cycle completed\n");
            #endif
        }
        // =======================================================
        
        // Check for data from UART (normal operation)
        if (Check_UART_Data_Available()) {
            data_buffer = UART_Read_Response(1000, &data_len);
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
                int max_display = (data_len > 50) ? 50 : data_len;
                for (int i = 0; i < max_display; i++) {
                    sprintf(hex_str + strlen(hex_str), "%02X ", data_buffer[i]);
                }
                snprintf((char*)receive_data, sizeof(receive_data), "Auto Data[%d bytes]: %s%s",
                         data_len, hex_str, (data_len > 50) ? "..." : "");
                pthread_mutex_unlock(&command_mutex);
                memset(hex_str, 0, sizeof(hex_str));
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
                        
                        uint8_t uart_cmd = hex_string_to_uint8(menu_item->hex_value);
                        if (uart_cmd != 0) {
                            UART_Write_Command(uart_cmd);
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

void *modbus_thread_func(void *arg) {
    uint16_t data_len = 0;
    data_frame_t *receive_frame = NULL;
    data_frame_t data_frame;
    time_t get_data_time = 0;
    uint8_t command_buffer[64] = {0};
    char response_str[256] = {0};
    char display_str[128] = {0};
    memset(display_str, 0, sizeof(display_str));
    int receive_num = 0;
    while (1) {
        pthread_mutex_lock(&modbus_pause.mutex);
        while (modbus_pause.is_paused) {
            pthread_cond_wait(&modbus_pause.cond, &modbus_pause.mutex);  // Sleep and wait for signal
        }
        pthread_mutex_unlock(&modbus_pause.mutex);
        if (config_reloading) {
            usleep(1000 * 1000); // Wait 1000ms during reload
            continue;
        }
        // Get data every 2 second
        time_t current_time = time(NULL);
        if ((current_time - get_data_time) >= 2) {
             pthread_mutex_lock(&config_mutex);
            if (!config_reloading) {
                for (int i = 0; i < get_node_count(); i++) {
                    node_config_t *node = get_node_by_index(i);
                    if (node && node->detection_commands && node->detection_count > 0 && strncmp(node->com_type, "Modbus", 6) == 0) {
                        data_frame.frame_length = hex_string_to_bytes(node->detection_commands[0].command, command_buffer, 256);
                        data_frame.frame_length += 3;
                        data_frame.address = hex_string_to_uint8(node->address);
                        data_frame.function.write = command_buffer[0];
                        data_frame.data = command_buffer + 1;
#ifdef DEBUG
                        printf("Modbus check node %d\n", node->node_id);
                        for (int j = 0; j < data_frame.frame_length - 3; j++) {
                            printf("%02X ", command_buffer[j]);
                        }
                        printf("\n");
#endif
                        // Send detection command from JSON
                        Modbus_Write_Frame(&data_frame);
                        
                        // Read response with timeout from JSON
                        int timeout_ms = node->detection_commands[0].timeout_ms;
                        if (timeout_ms <= 0) timeout_ms = 1000; // Default 1 second

                        receive_frame = Modbus_Read_Response(timeout_ms);

                        if (receive_frame) {
                            receive_num++;
                            // Process the received frame
                            // Convert received data to displayable format
                            memset(display_str, 0, sizeof(display_str));

                            sprintf(display_str, "%02X %02X %02X %02X", receive_frame->address, receive_frame->function.custom, receive_frame->data[0], receive_frame->data[1]);

                            #ifdef DEBUG
                            printf("Received modbus data: %s\n", display_str);
                            #endif
                            response_str[0] = receive_frame->address;
                            response_str[1] = receive_frame->function.custom;
                            for (int i = 0; i < receive_frame->frame_length  - 4; i++) {
                                response_str[i + 2] = receive_frame->data[i];
                            }
                            // Update status
                            pthread_mutex_lock(&command_mutex);
                            snprintf(status_response, sizeof(status_response),
                                     "Received modbus data: %d bytes", receive_frame->frame_length);
                            status_color = 2;
                            pthread_mutex_unlock(&command_mutex);
                            snprintf((char*)receive_data, sizeof(receive_data), "Auto Data[%d bytes]: %s%s, receive_num: %d",
                                     receive_frame->frame_length, display_str, (receive_frame->frame_length > 50) ? "..." : "", receive_num);
                            update_mqtt_data_from_response(node, response_str, receive_frame->frame_length - 2);
                            free(receive_frame);
                        }
                        usleep(5 * 1000); // Small delay for next command
                    }
                }
            }
            pthread_mutex_unlock(&config_mutex);
            
            get_data_time = current_time;
        }
        // =======================================================
        
        // Handle server control commands
        server_control_cmd_t control_cmd;
        if (get_control_command(&control_cmd) == 0) {
            pthread_mutex_lock(&config_mutex);
            if (!config_reloading) {
                node_config_t *node = get_node_by_id(control_cmd.node_id);
                if (node && strncmp(node->com_type, "Modbus", 6) == 0) {
                    menu_item_t *menu_item = get_menu_item_by_cmd(node, control_cmd.cmd_id);
                    if (menu_item) {
                        #ifdef DEBUG
                        printf("Executing server command: node=%d, cmd=%d\n",
                               control_cmd.node_id, control_cmd.cmd_id);
                        #endif
                        
                        uint8_t length = hex_string_to_bytes(menu_item->hex_value, command_buffer, 64);
                        data_frame.frame_length = length + 3;
                        data_frame.address = hex_string_to_uint8(node->address);
                        data_frame.function.write = command_buffer[0];
                        data_frame.data = command_buffer + 1;
                        if (length > 0) {
                            Modbus_Write_Frame(&data_frame);
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
    pause_thread(&uart_pause);
    pause_thread(&modbus_pause);
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
            "Select Communication Type",
            "Reload Configuration",
            "View Node Configuration",
            "Select Node Communication Type",
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
                        usleep(500 * 1000); // Small delay
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
                        usleep(500 * 1000); // Wait for 500ms to show message
                        key_check = getch();
                        while (key_check == ERR) {
                            key_check = getch(); // Wait for any key
                        }
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
                                             node->node_id, node->name, node->com_type,
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
                    case 5: // Select Node Communication Type
                        clear();
                        mvprintw(1, 0, "=== Select Node Communication Type ===");
                        // Display available communication types
                        const char *node_comm_types[] = {
                            "UART",
                            "Modbus",
                            "CAN (Coming soon)"
                        };
                        int node_comm_highlight = 0;
                        int node_comm_selecting = 1;

                        while (node_comm_selecting) {
                            for (int i = 0; i < sizeof(node_comm_types) / sizeof(node_comm_types[0]); i++) {
                                if (i == node_comm_highlight) {
                                    attron(COLOR_PAIR(1));
                                }
                                mvprintw(5 + i, 2, "%d. %s", i + 1, node_comm_types[i]);
                                if (i == node_comm_highlight) {
                                    attroff(COLOR_PAIR(1));
                                }
                            }

                            mvprintw(5 + sizeof(node_comm_types) / sizeof(node_comm_types[0]) + 2, 0, "Use UP/DOWN to select, ENTER to confirm, ESC to cancel");
                            refresh();

                            int node_comm_key = getch();
                            switch (node_comm_key) {
                                case KEY_UP:
                                    node_comm_highlight = (node_comm_highlight == 0) ? sizeof(node_comm_types) / sizeof(node_comm_types[0]) - 1 : node_comm_highlight - 1;
                                    break;
                                case KEY_DOWN:
                                    node_comm_highlight = (node_comm_highlight == sizeof(node_comm_types) / sizeof(node_comm_types[0]) - 1) ? 0 : node_comm_highlight + 1;
                                    break;
                                case 10: // ENTER
                                    if (node_comm_highlight == 0) {
                                        // UART selected
                                        Uart_Init(UART_map_to_speed(baudrate), device);
                                        pause_thread(&modbus_pause);
                                        resume_thread(&uart_pause);
                                    } else if (node_comm_highlight == 1) {
                                        // Modbus selected
                                        Modbus_Init(UART_map_to_speed(baudrate), device);
                                        pause_thread(&uart_pause);
                                        resume_thread(&modbus_pause);
                                    }
                                    mvprintw(5 + sizeof(node_comm_types) / sizeof(node_comm_types[0]) + 4, 0, "Node communication type changed to: %s",
                                             node_comm_types[node_comm_highlight]);
                                    refresh();
                                    sleep(1);
                                    node_comm_selecting = 0;
                                    break;
                                case 27: // ESC
                                    node_comm_selecting = 0;
                                    break;
                            }
                        }
                        break;
                    case 6: // Exit
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

// Convert hex string to integer
uint8_t hex_string_to_uint8(const char *hex_str)
{
    if (!hex_str)
        return 0;
    
    if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0)
    {
        hex_str += 2;
    }
    
    unsigned long result = strtoul(hex_str, NULL, 16);

    // Limit in range from 0x00 to 0xFF
    if (result > 0xFF)
        result = 0xFF;
        
    return (uint8_t)result;
}

// Convert hex string to byte array
size_t hex_string_to_bytes(const char *hex_str, uint8_t *output, size_t max_bytes)
{
    // Check for null pointers and invalid buffer size
    if (!hex_str || !output || max_bytes == 0)
        return 0;
    
    size_t byte_count = 0;
    // Create a copy of input string to avoid modifying the original
    char *str_copy = strdup(hex_str);
    // Get first token separated by space
    char *token = strtok(str_copy, " ");
    
    // Process each hex token until no more tokens or buffer is full
    while (token != NULL && byte_count < max_bytes)
    {
        // Skip "0x" or "0X" prefix if present
        if (strncmp(token, "0x", 2) == 0 || strncmp(token, "0X", 2) == 0)
            token += 2;
        
        // Convert hex token to unsigned long, then to byte
        unsigned long value = strtoul(token, NULL, 16);
        // Only store if value is within byte range (0-255)
        if (value <= 0xFF)
        {
            output[byte_count++] = (uint8_t)value;
        }
        
        // Get next token
        token = strtok(NULL, " ");
    }
    
    // Free the allocated memory for string copy
    free(str_copy);
    // Return number of bytes successfully parsed
    return byte_count;
}

// Pause thread function
void pause_thread(thread_pause_t *pause_ctrl) {
    pthread_mutex_lock(&pause_ctrl->mutex);
    pause_ctrl->is_paused = true;
    pthread_mutex_unlock(&pause_ctrl->mutex);
}

// Resume thread function
void resume_thread(thread_pause_t *pause_ctrl) {
    pthread_mutex_lock(&pause_ctrl->mutex);
    pause_ctrl->is_paused = false;
    pthread_cond_signal(&pause_ctrl->cond);
    pthread_mutex_unlock(&pause_ctrl->mutex);
}