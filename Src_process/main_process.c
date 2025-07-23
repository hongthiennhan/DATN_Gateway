#include "main.h"
#include "control_command.h"
#include "thread_func.h"

static struct option long_options[] = {
    {0, 0, 0, 0}
};

// ==================== MAIN FUNCTION ====================
int main(int argc, char **argv) {
    uint32_t baudrate = 115200;
    char *device = "/dev/ttyUSB0";
    int opt, option_index = 0;

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

    // Main waits for threads to finish (does nothing else)
    pthread_join(uart_thread, NULL);
    pthread_join(ui_thread, NULL);

    close(uart_fd);
    return 0;
}