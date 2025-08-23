#include "main.h"
#include "uart_handler.h"
#include "thread_func.h"
#include "node_config.h"

#define DEBUG_MAIN

static struct option long_options[] = {
    {0, 0, 0, 0}};

// Function to clean up resources on exit
void cleanup_on_exit(void)
{
    // Close UART
    if (uart_fd >= 0)
    {
        close(uart_fd);
    }

    // Cleanup node config
    cleanup_nodes_config();
    // Cleanup command data
    if (command_data.data)
    {
        free(command_data.data);
    }
    pthread_mutex_destroy(&command_data.mutex);
    pthread_cond_destroy(&command_data.cond);
    // Cleanup command mutex
    pthread_mutex_destroy(&command_mutex);
#ifdef DEBUG
    printf("Cleanup completed\n");
#endif
}

// ==================== MAIN FUNCTION ====================
int main(void)
{
    atexit(cleanup_on_exit);
    uint8_t load_try = 0;
    
    // Load node configuration FIRST
    if (load_nodes_config("../config.json") != 0)
    {
#ifdef DEBUG_MAIN
        fprintf(stderr, "Failed to load node configuration\n");
#endif
        load_try = 1;
    }
    else {
#ifdef DEBUG_MAIN
        printf("Node configuration loaded successfully\n");
#endif
    }

    // If first load failed, try fallback config
    if (load_try == 1)
    {
        if (load_nodes_config("../nodes_config.json") != 0)
        {
#ifdef DEBUG_MAIN
            fprintf(stderr, "Failed to load node configuration from fallback\n");
#endif
            return -1;
        }
    }

    // NEW: Check what types of nodes are configured
    uart_nodes_config_t *uart_config = get_uart_nodes_config();
    modbus_nodes_config_t *modbus_config = get_modbus_nodes_config();
    
    bool has_uart_nodes = (uart_config && uart_config->uart_count > 0);
    bool has_modbus_nodes = (modbus_config && modbus_config->modbus_count > 0);
    
#ifdef DEBUG_MAIN
    printf("Found %d UART nodes, %d Modbus nodes\n", 
           has_uart_nodes ? uart_config->uart_count : 0,
           has_modbus_nodes ? modbus_config->modbus_count : 0);
#endif

    // Get default values from config
    uint32_t baudrate = get_default_baudrate();
    char *device = strdup(get_default_device());
    
    // Initialize command_data
    command_data.data = malloc(sizeof(int));
    if (command_data.data == NULL)
    {
#ifdef DEBUG_MAIN
        fprintf(stderr, "Failed to allocate memory for command data\n");
#endif
        return -1;
    }
    *(int *)command_data.data = -1;

    // NEW: Conditional initialization based on node types
    pthread_t uart_thread, modbus_thread, ui_thread, mqtt_thread;
    
    // Initialize UART only if we have UART nodes
    if (has_uart_nodes)
    {
        Uart_Init(UART_map_to_speed(baudrate), device);
        Clear_Startup_UART(uart_fd, (uint32_t)get_startup_clear_duration());
        
        // Start UART thread
        pthread_create(&uart_thread, NULL, uart_thread_func, NULL);
#ifdef DEBUG_MAIN
        printf("UART thread started for %d nodes\n", uart_config->uart_count);
#endif
    }
    
    // NEW: Initialize Modbus if we have Modbus nodes
    if (has_modbus_nodes)
    {
        // Start Modbus thread (you'll need to implement modbus_thread_func)
        pthread_create(&modbus_thread, NULL, modbus_thread_func, NULL);
#ifdef DEBUG_MAIN
        printf("Modbus thread started for %d nodes\n", modbus_config->modbus_count);
#endif
    }
    
    // Prepare arguments for UI thread
    void *ui_args[4] = {&baudrate, device, &has_uart_nodes, &has_modbus_nodes};
    
    // Start UI and MQTT threads (always needed)
    pthread_create(&ui_thread, NULL, ui_thread_func, ui_args);
    pthread_create(&mqtt_thread, NULL, mqtt_thread_func, NULL);

    // Wait for active threads to finish
    if (has_uart_nodes)
        pthread_join(uart_thread, NULL);
    if (has_modbus_nodes)
        pthread_join(modbus_thread, NULL);
    
    pthread_join(ui_thread, NULL);
    pthread_join(mqtt_thread, NULL);

    free(device);
    return 0;
}
