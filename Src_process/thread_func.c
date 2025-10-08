#include "thread_func.h"
// Shared state between threads
unsigned char downlink_data[512] = {0};
pthread_mutex_t downlink_mutex = PTHREAD_MUTEX_INITIALIZER;
uint8_t check_downlink = 0;

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
          mqtt_update_received_data(data_buffer, data_len);
        } else if (server_check == 2) {
          tcp_update_received_data(data_buffer, data_len);
        } else if (server_check == 3) {
          udp_update_received_data(data_buffer, data_len);
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

  pause_thread(&mqtt_pause);
  pause_thread(&tcp_pause);
  pause_thread(&udp_pause);
#ifdef DEBUG
  printf("Config thread started, all server threads paused\n");
#endif
  system_config_t *sys_config = get_system_config();
  char *server_type = sys_config->server_com_type;
#ifdef DEBUG
  printf("Config thread: Detected config update, new server type: %s\n",
         server_type);
#endif
  while (1) {
    int updated = config_updated;
    if (updated) {
      system_config_t *sys_config = get_system_config();
      char *server_type = sys_config->server_com_type;
#ifdef DEBUG
      printf("Config thread: Detected config update, new server type: %s\n",
             server_type);
#endif
      if (strncmp(server_type, "MQTT", 4) == 0) {
        server_check = 1;
        pause_thread(&tcp_pause);
        resume_thread(&mqtt_pause);
        pause_thread(&udp_pause);
#ifdef DEBUG
        printf("Config thread: Switched to MQTT, TCP and UDP threads paused\n");
#endif
      } else if (strncmp(server_type, "TCP", 3) == 0) {
        server_check = 2;
        resume_thread(&tcp_pause);
        pause_thread(&mqtt_pause);
        pause_thread(&udp_pause);
#ifdef DEBUG
        printf("Config thread: Switched to TCP, MQTT and UDP threads paused\n");
#endif
      } else if (strncmp(server_type, "UDP", 3) == 0) {
        server_check = 3;
        pause_thread(&tcp_pause);
        pause_thread(&mqtt_pause);
        resume_thread(&udp_pause);
#ifdef DEBUG
        printf("Config thread: Switched to UDP, MQTT and TCP threads paused\n");
#endif
      }
    }
  }
  return NULL;
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