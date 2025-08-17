#include "main.h"
#include "control_command.h"
#include "thread_func.h"
#include "node_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

static struct option long_options[] = {
    {0, 0, 0, 0}
};

// Function to clean up resources on exit with improved safety
void cleanup_on_exit(void) {
    #ifdef DEBUG
    printf("Starting cleanup process...\n");
    #endif
    
    // Close UART
    if (uart_fd >= 0) {
        close(uart_fd);
        #ifdef DEBUG
        printf("UART closed\n");
        #endif
    }
    
    // Cleanup node config
    cleanup_nodes_config();
    #ifdef DEBUG
    printf("Node config cleaned up\n");
    #endif
    
    // Cleanup command data safely
    if (command_data.data) {
        free(command_data.data);
        command_data.data = NULL;
    }
    
    // Destroy mutexes/conditions safely
    pthread_mutex_destroy(&command_data.mutex);
    pthread_cond_destroy(&command_data.cond);
    pthread_mutex_destroy(&command_mutex);
    
    #ifdef DEBUG
    printf("Cleanup completed\n");
    #endif
}

// Main function with improved error handling
int main(void) {
    #ifdef DEBUG
    printf("CHECKPOINT: Starting main process\n");
    #endif
    
    // Register cleanup function to be called on exit
    atexit(cleanup_on_exit);
    
    uint8_t load_try = 0;
    int config_loaded = 0;
    
    // Load node configuration FIRST with fallback handling
    #ifdef DEBUG
    printf("CHECKPOINT: Attempting to load configuration\n");
    #endif
    
    if (load_nodes_config("../config.json") == 0) {
        config_loaded = 1;
        #ifdef DEBUG
        printf("CHECKPOINT: Loaded config.json successfully\n");
        #endif
    } else {
        #ifdef DEBUG
        fprintf(stderr, "Failed to load ../config.json\n");
        #endif
        load_try = 1;
    }
    
    // If first load failed, try fallback config
    if (load_try == 1) {
        if (load_nodes_config("nodes_config.json") == 0) {
            config_loaded = 1;
            #ifdef DEBUG
            printf("CHECKPOINT: Loaded nodes_config.json successfully\n");
            #endif
        } else {
            #ifdef DEBUG
            fprintf(stderr, "Failed to load fallback configuration\n");
            #endif
        }
    }
    
    // Ensure we have a valid configuration
    if (!config_loaded) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: No valid configuration could be loaded\n");
        #endif
        return -1;
    }
    
    // Get default values from config with safety checks
    uint32_t baudrate = get_default_baudrate();
    const char *default_device = get_default_device();
    char *device = strdup(default_device ? default_device : "/dev/ttyUSB0");
    
    if (!device) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to duplicate device string\n");
        #endif
        return -1;
    }
    
    #ifdef DEBUG
    printf("CHECKPOINT: Using UART device: %s @ %u baud\n", device, baudrate);
    #endif
    
    // Initialize command_data with proper error checking
    command_data.data = malloc(sizeof(int));
    if (command_data.data == NULL) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to allocate memory for command data\n");
        #endif
        free(device);
        return -1;
    }
    *(int *)command_data.data = -1; // Initialize to -1
    
    // Load additional config if needed (this might be a custom function)
    uint8_t config_result = load_config(&baudrate, &device);
    if (config_result != 0) {
        #ifdef DEBUG
        printf("Warning: load_config returned %d\n", config_result);
        #endif
    }
    
    // Initialize UART
    #ifdef DEBUG
    printf("CHECKPOINT: Initializing UART\n");
    #endif
    Uart_Init(map_to_speed(baudrate), device);
    
    // Clear startup UART with safety check
    uint32_t clear_duration = get_startup_clear_duration();
    if (clear_duration > 0) {
        Clear_Startup_UART(uart_fd, clear_duration);
    }
    
    // Prepare arguments for UI thread
    void *ui_args[2] = {&baudrate, device};
    
    // Start threads with error checking
    #ifdef DEBUG
    printf("CHECKPOINT: Starting threads\n");
    #endif
    
    pthread_t uart_thread, ui_thread, mqtt_thread;
    int uart_result, ui_result, mqtt_result;
    
    // Start UART thread
    uart_result = pthread_create(&uart_thread, NULL, uart_thread_func, NULL);
    if (uart_result != 0) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to create UART thread: %d\n", uart_result);
        #endif
        free(device);
        return -1;
    }
    
    // Start UI thread
    ui_result = pthread_create(&ui_thread, NULL, ui_thread_func, ui_args);
    if (ui_result != 0) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to create UI thread: %d\n", ui_result);
        #endif
        // Cancel UART thread if UI fails
        pthread_cancel(uart_thread);
        free(device);
        return -1;
    }
    
    // Start MQTT thread
    mqtt_result = pthread_create(&mqtt_thread, NULL, mqtt_thread_func, NULL);
    if (mqtt_result != 0) {
        #ifdef DEBUG
        fprintf(stderr, "ERROR: Failed to create MQTT thread: %d\n", mqtt_result);
        #endif
        // Cancel other threads if MQTT fails
        pthread_cancel(uart_thread);
        pthread_cancel(ui_thread);
        free(device);
        return -1;
    }
    
    #ifdef DEBUG
    printf("CHECKPOINT: All threads started successfully\n");
    #endif
    
    // Main waits for threads to finish
    #ifdef DEBUG
    printf("CHECKPOINT: Waiting for threads to complete\n");
    #endif
    
    pthread_join(uart_thread, NULL);
    pthread_join(ui_thread, NULL);
    pthread_join(mqtt_thread, NULL);
    
    // Cleanup the device string
    free(device);
    
    #ifdef DEBUG
    printf("CHECKPOINT: Main process completed normally\n");
    #endif
    
    return 0;
}
