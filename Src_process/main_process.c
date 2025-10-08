#include "main.h"
#include "thread_func.h"

#define DEBUG_MAIN

// ==================== MAIN FUNCTION ====================
int main(void) {
  uint8_t load_try = 0;
  // Load gateway configuration FIRST
  if (load_gateway_config("../config.json") != 0) {
#ifdef DEBUG_MAIN
    fprintf(stderr, "Failed to load gateway configuration\n");
#endif
    load_try = 1;
  } else {
#ifdef DEBUG_MAIN
    printf("Gateway configuration loaded successfully\n");
#endif
  }

  // If first load failed, try fallback config
  if (load_try == 1) {
    if (load_gateway_config("../nodes_config.json") != 0) {
#ifdef DEBUG_MAIN
      fprintf(stderr, "Failed to load node configuration from fallback\n");
#endif
      return -1;
    }
  }

  system_config_t *sys_config = get_system_config();
  uint32_t baudrate = sys_config->default_baudrate;
  char *device = strdup(sys_config->default_device);

  Uart_Init(UART_map_to_speed(baudrate), device);

  //   // Start Data thread
  pthread_t data_thread;
  pthread_create(&data_thread, NULL, data_thread_func, NULL);
  // Start Config thread
  pthread_t config_thread;
  pthread_create(&config_thread, NULL, config_thread_func, NULL);
  // Start MQTT thread
  pthread_t mqtt_thread;
  pthread_create(&mqtt_thread, NULL, mqtt_thread_func, NULL);
  // Start TCP thread
  pthread_t tcp_thread;
  pthread_create(&tcp_thread, NULL, tcp_thread_func, NULL);
  pthread_t udp_thread;
  pthread_create(&udp_thread, NULL, udp_thread_func, NULL);

  // Main waits for threads to finish (does nothing else)
  pthread_join(mqtt_thread, NULL);
  pthread_join(tcp_thread, NULL);
  pthread_join(udp_thread, NULL);
  // Cleanup the device string
  free(device);
  return 0;
}