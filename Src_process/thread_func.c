#include "thread_func.h"
#include <ncurses.h>
#include "node_config.h"

// Shared state between threads
volatile uint8_t is_busy = 0;
pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
int command_pending = 0;
char status_response[100] = "System ready";
int status_color = 2;
unsigned char receive_data = {0};
int shared_node_type = 0;

shared_data_t command_data = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// ==================== UART THREAD - Data receive + Server commands ====================
void *uart_thread_func(void *arg) {
    printf("UART thread started - data receive mode\n");
    
    uint16_t resp_len = 0;
    unsigned char *resp = NULL;
    server_control_cmd_t control_cmd;
    time_t last_detection = 0;
    
    while (1) {
        time_t current_time = time(NULL);
        
        // Execute server control commands
        if (get_control_command(&control_cmd) == 0) {
            node_config_t *node = get_node_by_id(control_cmd.node_id);
            if (node) {
                menu_item_t *menu_item = get_menu_item_by_cmd(node, control_cmd.cmd_id);
                if (menu_item) {
                    printf("Executing server command: node=%d, cmd=%d\n",
                           control_cmd.node_id, control_cmd.cmd_id);
                    execute_uart_command(node, menu_item, &resp, &resp_len, 0);
                    if (resp) {
                        free(resp);
                        resp = NULL;
                        resp_len = 0;
                    }
                }
            }
            continue;
        }
        
        // Periodic node detection
        int detection_interval = get_detection_interval();
        if (detection_interval > 0 && (current_time - last_detection) >= detection_interval) {
            printf("Starting periodic node detection\n");
            start_node_detection();
            last_detection = current_time;
        }
        
        // Auto-read from detected nodes
        for (int i = 0; i < get_node_count(); i++) {
            node_config_t *node = get_node_by_index(i);
            if (node && node->detected && node->auto_read_interval > 0) {
                time_t last_read = (node->mqtt_data && node->mqtt_data->data) ?
                                   ((raw_data_t*)node->mqtt_data->data)->length : 0;
                                   
                if ((current_time - last_read) >= node->auto_read_interval) {
                    menu_item_t *auto_read_item = get_menu_item_by_cmd(node, node->auto_read_cmd);
                    if (auto_read_item) {
                        execute_uart_command(node, auto_read_item, &resp, &resp_len, 1);
                        if (resp) {
                            free(resp);
                            resp = NULL;
                            resp_len = 0;
                        }
                    }
                }
            }
        }
        
        usleep(1000 * 1000); // 1 second sleep
    }
    
    return NULL;
}

/**
 * Execute UART command for node
 */
void execute_uart_command(node_config_t *node, menu_item_t *menu_item, unsigned char **resp, uint16_t *resp_len, int silent) {
    *resp = NULL;
    *resp_len = 0;
    
    uint32_t uart_cmd = hex_string_to_int(menu_item->hex_value);
    if (uart_cmd == 0) return;
    
    write_command((uint8_t)uart_cmd);
    
    if (menu_item->timeout_ms > 0) {
        *resp = Read_Response(menu_item->timeout_ms, resp_len);
    }
    
    process_uart_response(node, menu_item->cmd, *resp, *resp_len, silent);
}

/**
 * Process UART response from node
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

// ==================== UI THREAD - Simple config menu ====================
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
    timeout(1000); // 1 second timeout for refresh
    
    int highlight = 0;
    
    while (1) {
        clear();
        
        // Header
        attron(COLOR_PAIR(4));
        mvprintw(0, 0, "===== IoT Gateway Configuration Menu =====");
        attroff(COLOR_PAIR(4));
        
        // System info
        mvprintw(2, 0, "UART: %s @ %u baud", device, baudrate);
        mvprintw(3, 0, "Nodes detected: %d", get_node_count());
        
        pthread_mutex_lock(&command_mutex);
        attron(COLOR_PAIR(status_color));
        mvprintw(4, 0, "Status: %s", status_response);
        attroff(COLOR_PAIR(status_color));
        
        if (strlen((char*)receive_data) > 0) {
            attron(COLOR_PAIR(5));
            mvprintw(5, 0, "Last data: %s", receive_data);
            attroff(COLOR_PAIR(5));
        }
        pthread_mutex_unlock(&command_mutex);
        
        // Menu options
        const char *menu_options[] = {
            "View System Status",
            "View MQTT Configuration", 
            "Reload Configuration",
            "View Node Configuration",
            "Exit"
        };
        
        int num_options = sizeof(menu_options) / sizeof(menu_options[0]);
        mvprintw(7, 0, "Configuration Options:");
        
        for (int i = 0; i < num_options; i++) {
            if (i == highlight) {
                attron(COLOR_PAIR(1));
            }
            mvprintw(9 + i, 2, "%d. %s", i + 1, menu_options[i]);
            if (i == highlight) {
                attroff(COLOR_PAIR(1));
            }
        }
        
        // Instructions
        attron(COLOR_PAIR(5));
        mvprintw(9 + num_options + 2, 0, "Use UP/DOWN arrows and ENTER to select");
        mvprintw(9 + num_options + 3, 0, "Press 'q' to quit, 'r' to refresh");
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
                            mvprintw(10, 0, "Detected Nodes: %d", get_node_count());
                        }
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        getch();
                        break;
                        
                    case 1: // View MQTT Configuration
                        clear();
                        mqtt_config_t *mqtt_cfg = get_mqtt_config();
                        if (mqtt_cfg) {
                            mvprintw(1, 0, "=== MQTT Configuration ===");
                            mvprintw(3, 0, "Broker: %s:%d", mqtt_cfg->broker_host, mqtt_cfg->broker_port);
                            mvprintw(4, 0, "Client ID: %s", mqtt_cfg->client_id);
                            mvprintw(5, 0, "Username: %s", mqtt_cfg->username);
                            mvprintw(6, 0, "Topic Telemetry: %s", mqtt_cfg->topic_telemetry);
                            mvprintw(7, 0, "Topic Control: %s", mqtt_cfg->topic_control);
                            mvprintw(8, 0, "Topic Status: %s", mqtt_cfg->topic_status);
                            mvprintw(9, 0, "QoS: %d", mqtt_cfg->qos);
                            mvprintw(10, 0, "Publish Interval: %d sec", mqtt_cfg->publish_interval);
                        }
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        getch();
                        break;
                        
                    case 2: // Reload Configuration
                        clear();
                        mvprintw(1, 0, "Reloading configuration...");
                        refresh();
                        
                        cleanup_nodes_config();
                        int load_result = 0;
                        if (load_nodes_config("../config.json") == 0) {
                            load_result = 1;
                        } else if (load_nodes_config("nodes_config.json") == 0) {
                            load_result = 1;
                        }
                        
                        if (load_result) {
                            attron(COLOR_PAIR(2));
                            mvprintw(3, 0, "Configuration reloaded successfully!");
                            mvprintw(4, 0, "Loaded %d nodes", get_node_count());
                            attroff(COLOR_PAIR(2));
                        } else {
                            attron(COLOR_PAIR(3));
                            mvprintw(3, 0, "Failed to reload configuration!");
                            attroff(COLOR_PAIR(3));
                        }
                        
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        getch();
                        break;
                        
                    case 3: // View Node Configuration
                        clear();
                        mvprintw(1, 0, "=== Node Configuration ===");
                        int node_count = get_node_count();
                        if (node_count > 0) {
                            for (int i = 0; i < node_count; i++) {
                                node_config_t *node = get_node_by_index(i);
                                if (node) {
                                    attron(node->detected ? COLOR_PAIR(2) : COLOR_PAIR(3));
                                    mvprintw(3 + i, 0, "Node %d: %s (%s) - %s",
                                             node->node_id, node->name, node->type,
                                             node->detected ? "DETECTED" : "NOT DETECTED");
                                    attroff(node->detected ? COLOR_PAIR(2) : COLOR_PAIR(3));
                                }
                            }
                        } else {
                            mvprintw(3, 0, "No nodes configured");
                        }
                        
                        mvprintw(LINES - 2, 0, "Press any key to continue...");
                        refresh();
                        getch();
                        break;
                        
                    case 4: // Exit
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
