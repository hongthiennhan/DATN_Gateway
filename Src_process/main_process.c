#include "main.h"
#include "control_command.h"
#include "thread_func.h"

static struct option long_options[] = {
    {0, 0, 0, 0}
};
// Function to clean up resources on exit
void cleanup_on_exit(void) {
    close(uart_fd);
    if (command_data.data) {
        free(command_data.data);
    }
    pthread_mutex_destroy(&command_data.mutex);
    pthread_cond_destroy(&command_data.cond);
    sprintf("Cleanup completed via atexit()\n");
}


// ==================== MAIN FUNCTION ====================
int main(int argc, char **argv) {
    atexit(cleanup_on_exit);  // Register cleanup function to be called on exit
    uint32_t baudrate = 115200;
    char *device = "/dev/ttyUSB0";
    int opt, option_index = 0;
    // Initialize command_data with a pointer to an int
    command_data.data = malloc(sizeof(int));
    if (command_data.data == NULL) {
        fprintf(stderr, "Failed to allocate memory for command data\n");
        return -1;
    }
    *(int*)command_data.data = -1;  // Initialize to -1
    // Parse command line arguments
    while ((opt = getopt_long(argc, argv, "B:d:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'B':
                baudrate = atoi(optarg);
                if (map_to_speed(baudrate) == B0) baudrate = 115200;
                break;
            case 'd':
                device = optarg;
                break;
            case '?':
                fprintf(stderr, "Usage: %s [-B baudrate] [-d device]\n", argv[0]);
                exit(1);
            default:
                break;
        }
    }

    load_config(&baudrate, &device);
    Uart_Init(map_to_speed(baudrate), device);
    Clear_Startup_UART(uart_fd, 10000);

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
    return 0;
}
