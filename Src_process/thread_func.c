#include "thread_func.h"
// Shared state between threads
volatile uint8_t is_busy = 0;
pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
char status_response[256] = "System ready";
uint32_t status_color = 2;
unsigned char uplink_data[512] = {0};
pthread_mutex_t uplink_mutex = PTHREAD_MUTEX_INITIALIZER;
uint8_t check_uplink = 0;
unsigned char downlink_data[512] = {0};
pthread_mutex_t downlink_mutex = PTHREAD_MUTEX_INITIALIZER;
uint8_t check_downlink = 0;
uint8_t CAN_TYPE = 0; // 0: USB, 1: CUSTOM

// shared_data_t command_data = {
//     .data = NULL,
//     .mutex = PTHREAD_MUTEX_INITIALIZER,
//     .cond = PTHREAD_COND_INITIALIZER};
thread_pause_t uart_pause = {
    .is_paused = false,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER};

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
void pause_thread(thread_pause_t *pause_ctrl)
{
    pthread_mutex_lock(&pause_ctrl->mutex);
    pause_ctrl->is_paused = true;
    pthread_mutex_unlock(&pause_ctrl->mutex);
}

// Resume thread function
void resume_thread(thread_pause_t *pause_ctrl)
{
    pthread_mutex_lock(&pause_ctrl->mutex);
    pause_ctrl->is_paused = false;
    pthread_cond_signal(&pause_ctrl->cond);
    pthread_mutex_unlock(&pause_ctrl->mutex);
}