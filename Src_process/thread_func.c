#include "thread_func.h"
// Shared state between threads
static uint8_t server_check = 1;

thread_pause_t config_thread_pause = {.is_paused = false,
                                      .mutex = PTHREAD_MUTEX_INITIALIZER,
                                      .cond = PTHREAD_COND_INITIALIZER};
thread_pause_t data_thread_pause = {.is_paused = false,
                                    .mutex = PTHREAD_MUTEX_INITIALIZER,
                                    .cond = PTHREAD_COND_INITIALIZER};

void *data_thread_func(void *arg) {

  uint16_t data_len = 0;
  unsigned char *data_buffer = NULL;
  while (1) {
    if (Check_UART_Data_Available()) {
      data_buffer = UART_Read_Response(1000, &data_len);
      if (data_buffer != NULL && data_len > 0) {
        if (server_check == 1) {
          update_mqtt_received_data(data_buffer, data_len);
        } else if (server_check == 2) {
          update_tcp_received_data(data_buffer, data_len);
        } else if (server_check == 3) {
          update_udp_received_data(data_buffer, data_len);
        }
#ifdef DEBUG
        printf("Data thread received %d bytes from UART\n", data_len);
#endif
      }
      free(data_buffer);
      data_buffer = NULL;
      data_len = 0;
    }
  }
  return NULL;
}

void *config_thread_func(void *arg) {
  while (1) {
    pthread_mutex_lock(&config_update_mutex);
    int updated = config_updated;
    pthread_mutex_unlock(&config_update_mutex);
    if (updated) {
      system_config_t *sys_config = get_system_config();
      char *server_type = sys_config->server_com_type;
      if (strncmp(server_type, "MQTT", 4) == 0) {
        server_check = 1;
        pause_thread(&tcp_pause);
        resume_thread(&mqtt_pause);
        pause_thread(&udp_pause);
      } else if (strncmp(server_type, "TCP", 3) == 0) {
        server_check = 2;
        resume_thread(&tcp_pause);
        pause_thread(&mqtt_pause);
        pause_thread(&udp_pause);
      } else if (strncmp(server_type, "UDP", 3) == 0) {
        server_check = 3;
        pause_thread(&tcp_pause);
        pause_thread(&mqtt_pause);
        resume_thread(&udp_pause);
      }
    }
  }
  return NULL;
}

// Convert hex string to integer
uint8_t hex_string_to_uint8(const char *hex_str) {
  if (!hex_str)
    return 0;

  if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0) {
    hex_str += 2;
  }

  unsigned long result = strtoul(hex_str, NULL, 16);

  // Limit in range from 0x00 to 0xFF
  if (result > 0xFF)
    result = 0xFF;

  return (uint8_t)result;
}

// Convert hex string to byte array
size_t hex_string_to_bytes(const char *hex_str, uint8_t *output,
                           size_t max_bytes) {
  // Check for null pointers and invalid buffer size
  if (!hex_str || !output || max_bytes == 0)
    return 0;

  size_t byte_count = 0;
  // Create a copy of input string to avoid modifying the original
  char *str_copy = strdup(hex_str);
  // Get first token separated by space
  char *token = strtok(str_copy, " ");

  // Process each hex token until no more tokens or buffer is full
  while (token != NULL && byte_count < max_bytes) {
    // Skip "0x" or "0X" prefix if present
    if (strncmp(token, "0x", 2) == 0 || strncmp(token, "0X", 2) == 0)
      token += 2;

    // Convert hex token to unsigned long, then to byte
    unsigned long value = strtoul(token, NULL, 16);
    // Only store if value is within byte range (0-255)
    if (value <= 0xFF) {
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