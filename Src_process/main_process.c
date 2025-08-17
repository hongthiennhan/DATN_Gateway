#include "main.h"
#include "control_command.h"
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
    atexit(cleanup_on_exit); // Register cleanup function to be called on exit
    uint8_t load_try = 0;
    // Load node configuration FIRST
    if (load_nodes_config("../config.json") != 0)
    {
#ifdef DEBUG_MAIN
        fprintf(stderr, "Failed to load node configuration\n");
#endif
        load_try = 1;
    }

    // If first load failed, try fallback config
    if (load_try == 1)
    {
        if (load_nodes_config("nodes_config.json") != 0)
        {
#ifdef DEBUG_MAIN
            fprintf(stderr, "Failed to load node configuration from fallback\n");
#endif
            return -1;
        }
    }

    // Get default values from config instead of hard-coding
    uint32_t baudrate = get_default_baudrate();
    char *device = strdup(get_default_device()); // Make a copy since it might be modified
    // Initialize command_data with a pointer to an int
    command_data.data = malloc(sizeof(int));
    if (command_data.data == NULL)
    {
#ifdef DEBUG_MAIN
        fprintf(stderr, "Failed to allocate memory for command data\n");
#endif
        return -1;
    }
    *(int *)command_data.data = -1; // Initialize to -1

    // Fixed: load_config now returns uint8_t
    uint8_t config_result = load_config(&baudrate, &device);
    if (config_result != 0)
    {
#ifdef DEBUG_MAIN
        printf("Warning: load_config returned %d\n", config_result);
#endif
    }

    Uart_Init(map_to_speed(baudrate), device);
    // Use config value instead of hard-coded timeout
    // Fixed: Clear_Startup_UART with uint32_t parameter
    Clear_Startup_UART(uart_fd, (uint32_t)get_startup_clear_duration());

    // Prepare arguments for UI thread: baudrate and device only (node selection moved to UI thread)
    void *ui_args[2] = {&baudrate, device};
    // Start UART thread (no arg needed, it will wait for shared_node_type)
    pthread_t uart_thread;
    pthread_create(&uart_thread, NULL, uart_thread_func, NULL);
    // Start UI thread
    pthread_t ui_thread;
    pthread_create(&ui_thread, NULL, ui_thread_func, ui_args);
    pthread_t mqtt_thread;
    pthread_create(&mqtt_thread, NULL, mqtt_thread_func, NULL);

    // Main waits for threads to finish (does nothing else)
    pthread_join(uart_thread, NULL);
    pthread_join(ui_thread, NULL);
    pthread_join(mqtt_thread, NULL);

    // Cleanup the device string
    free(device);
    return 0;
}
