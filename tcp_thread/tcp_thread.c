#include "tcp_thread.h"

// Static variables for TCP management
static struct mg_mgr tcp_mgr;
static struct mg_connection *tcp_connection = NULL;
volatile int tcp_connected = 0;

thread_pause_t tcp_pause = {.is_paused = false,
                            .mutex = PTHREAD_MUTEX_INITIALIZER,
                            .cond = PTHREAD_COND_INITIALIZER};

// Static buffer for storing received data
static pthread_mutex_t tcp_data_mutex = PTHREAD_MUTEX_INITIALIZER;
static char tcp_last_received_data[MAX_RECEIVED_DATA_SIZE] = {0};
static time_t tcp_last_received_time = 0;
static int tcp_total_messages_received = 0;

// Message acknowledgment tracking
typedef struct {
  uint32_t message_id;
  time_t send_time;
  int ack_received;
  int retry_count;
  char payload[1024];
  size_t payload_len;
} tcp_message_t;

#define MAX_PENDING_MESSAGES 16
#define ACK_TIMEOUT_MS 5000
#define MAX_RETRY_COUNT 3

static tcp_message_t pending_messages[MAX_PENDING_MESSAGES];
static pthread_mutex_t ack_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t next_message_id = 1;

// Forward declarations
static void tcp_event_handler(struct mg_connection *c, int ev, void *ev_data);
static uint32_t generate_message_id(void);
static int store_pending_message(uint32_t msg_id, const char *data, size_t len);
static void mark_message_acknowledged(uint32_t msg_id);
static void check_message_timeouts(void);
static int is_ack_message(const char *data);
static uint32_t extract_message_id_from_ack(const char *data);
static void send_ack_response(uint32_t msg_id);

// Generate unique message ID
static uint32_t generate_message_id(void) {
  pthread_mutex_lock(&ack_mutex);
  uint32_t id = next_message_id++;
  if (next_message_id == 0)
    next_message_id = 1; // Avoid 0
  pthread_mutex_unlock(&ack_mutex);
  return id;
}

// Store message for ACK tracking
static int store_pending_message(uint32_t msg_id, const char *data,
                                 size_t len) {
  pthread_mutex_lock(&ack_mutex);

  // Find empty slot
  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
    if (pending_messages[i].message_id == 0) {
      pending_messages[i].message_id = msg_id;
      pending_messages[i].send_time = time(NULL);
      pending_messages[i].ack_received = 0;
      pending_messages[i].retry_count = 0;

      size_t copy_len = len < sizeof(pending_messages[i].payload)
                            ? len
                            : sizeof(pending_messages[i].payload) - 1;
      memcpy(pending_messages[i].payload, data, copy_len);
      pending_messages[i].payload_len = copy_len;

      pthread_mutex_unlock(&ack_mutex);
      return 0;
    }
  }

  pthread_mutex_unlock(&ack_mutex);
  return -1; // No space available
}

// Mark message as acknowledged
static void mark_message_acknowledged(uint32_t msg_id) {
  pthread_mutex_lock(&ack_mutex);

  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
    if (pending_messages[i].message_id == msg_id) {
      pending_messages[i].ack_received = 1;
      pending_messages[i].message_id = 0; // Clear slot
      break;
    }
  }

  pthread_mutex_unlock(&ack_mutex);

#ifdef DEBUG
  printf("TCP: Message %u acknowledged\n", msg_id);
#endif
}

// Check for message timeouts and retry
static void check_message_timeouts(void) {
  time_t current_time = time(NULL);

  pthread_mutex_lock(&ack_mutex);

  for (int i = 0; i < MAX_PENDING_MESSAGES; i++) {
    if (pending_messages[i].message_id != 0 &&
        !pending_messages[i].ack_received) {
      time_t elapsed = current_time - pending_messages[i].send_time;

      if (elapsed * 1000 >= ACK_TIMEOUT_MS) {
        if (pending_messages[i].retry_count < MAX_RETRY_COUNT) {
          // Retry sending message
          if (tcp_connection && tcp_connected) {
            mg_send(tcp_connection, pending_messages[i].payload,
                    pending_messages[i].payload_len);
            pending_messages[i].send_time = current_time;
            pending_messages[i].retry_count++;

#ifdef DEBUG
            printf("TCP: Retrying message %u (attempt %d)\n",
                   pending_messages[i].message_id,
                   pending_messages[i].retry_count + 1);
#endif
          }
        } else {
          // Max retries reached, give up
#ifdef DEBUG
          printf("TCP: Message %u failed after %d retries\n",
                 pending_messages[i].message_id, MAX_RETRY_COUNT);
#endif
          pending_messages[i].message_id = 0; // Clear slot
        }
      }
    }
  }

  pthread_mutex_unlock(&ack_mutex);
}

// Check if received data is an ACK message
static int is_ack_message(const char *data) {
  return (strstr(data, "\"type\":\"ack\"") != NULL);
}

// Extract message ID from ACK
static uint32_t extract_message_id_from_ack(const char *data) {
  const char *id_start = strstr(data, "\"message_id\":");
  if (id_start) {
    id_start += strlen("\"message_id\":");
    return (uint32_t)atol(id_start);
  }
  return 0;
}

// Send ACK response for received message
static void send_ack_response(uint32_t msg_id) {
  if (!tcp_connection || !tcp_connected)
    return;

  tcp_config_t *config = get_tcp_config();
  if (!config)
    return;

  char ack_msg[256];
  snprintf(ack_msg, sizeof(ack_msg),
           "{\"type\":\"ack\",\"message_id\":%u,\"client_id\":\"%s\","
           "\"timestamp\":%ld}%s",
           msg_id, config->client_id, time(NULL),
           config->protocol_settings.message_delimiter);

  mg_send(tcp_connection, ack_msg, strlen(ack_msg));

#ifdef DEBUG
  printf("TCP: Sent ACK for message %u\n", msg_id);
#endif
}

// Get local IP address for gateway identification
char *tcp_get_local_ip(void) {
  static char ip_str[INET_ADDRSTRLEN];
  struct ifaddrs *ifaddrs_ptr, *ifa;

  if (getifaddrs(&ifaddrs_ptr) == -1) {
    strcpy(ip_str, "127.0.0.1");
    return ip_str;
  }

  // Find first non-loopback IPv4 address
  for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL)
      continue;
    if (ifa->ifa_addr->sa_family == AF_INET) {
      struct sockaddr_in *addr_in = (struct sockaddr_in *)ifa->ifa_addr;
      char *addr_str = inet_ntoa(addr_in->sin_addr);
      if (strncmp(addr_str, "127.", 4) != 0 &&
          strncmp(addr_str, "169.254.", 8) != 0) {
        strcpy(ip_str, addr_str);
        freeifaddrs(ifaddrs_ptr);
        return ip_str;
      }
    }
  }

  freeifaddrs(ifaddrs_ptr);
  strcpy(ip_str, "127.0.0.1");
  return ip_str;
}

// Helper function to escape JSON strings
static void escape_json_string(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0)
    return;

  size_t src_len = strlen(src);
  size_t dst_idx = 0;

  for (size_t i = 0; i < src_len && dst_idx < dst_size - 1; i++) {
    if (src[i] == '"' || src[i] == '\\') {
      if (dst_idx < dst_size - 2) {
        dst[dst_idx++] = '\\';
        dst[dst_idx++] = src[i];
      }
    } else if (src[i] >= 32 && src[i] <= 126) { // Printable ASCII
      dst[dst_idx++] = src[i];
    }
  }

  dst[dst_idx] = '\0';
}

// Helper function to convert data to hex string
static char *data_to_hex_string(const unsigned char *data, size_t len) {
  if (!data || len == 0)
    return NULL;

  char *hex_str = malloc(len * 2 + 1);
  if (!hex_str)
    return NULL;

  for (size_t i = 0; i < len; i++) {
    sprintf(hex_str + i * 2, "%02X", data[i]);
  }

  hex_str[len * 2] = '\0';
  return hex_str;
}

// TCP initialization
static int tcp_init(void) {
  mg_mgr_init(&tcp_mgr);
  tcp_connection = NULL;
  tcp_connected = 0;

  // Clear pending messages
  pthread_mutex_lock(&ack_mutex);
  memset(pending_messages, 0, sizeof(pending_messages));
  next_message_id = 1;
  pthread_mutex_unlock(&ack_mutex);

  tcp_config_t *config = get_tcp_config();
  if (!config || !strlen(config->server_host) || !config->server_port) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: Invalid TCP configuration\n");
#endif
    return -1;
  }

#ifdef DEBUG
  printf("TCP initialized: %s:%d (client: %s, protocol: %s)\n",
         config->server_host, config->server_port, config->client_id,
         config->protocol_version);
#endif
  return 0;
}

// TCP connect
static int tcp_connect(const char *server_url) {
  tcp_config_t *config = get_tcp_config();
  if (!config)
    return -1;

  char url[256];
  if (server_url && strlen(server_url) > 0) {
    snprintf(url, sizeof(url), "%s", server_url);
  } else {
    snprintf(url, sizeof(url), "tcp://%s:%d", config->server_host,
             config->server_port);
  }

  tcp_connected = 0;
  tcp_connection = mg_connect(&tcp_mgr, url, tcp_event_handler, NULL);

#ifdef DEBUG
  printf("TCP: %s to %s\n", tcp_connection ? "Connecting" : "Failed to connect",
         url);
#endif
  return tcp_connection ? 0 : -1;
}

// TCP close
static void tcp_close(void) {
  tcp_connected = 0;
  if (tcp_connection) {
    mg_mgr_poll(&tcp_mgr, 0);
    tcp_connection = NULL;
  }
  mg_mgr_free(&tcp_mgr);

#ifdef DEBUG
  printf("TCP: Connection closed\n");
#endif
}

// TCP reconnect
static int tcp_reconnect(void) {
  tcp_config_t *config = get_tcp_config();
  if (!config)
    return -1;

  if (tcp_connection) {
    mg_mgr_poll(&tcp_mgr, 0);
    tcp_connection = NULL;
  }

  tcp_connected = 0;
  char url[256];
  snprintf(url, sizeof(url), "tcp://%s:%d", config->server_host,
           config->server_port);
  tcp_connection = mg_connect(&tcp_mgr, url, tcp_event_handler, NULL);

  return tcp_connection ? 0 : -1;
}

// Enhanced send data with acknowledgment support
static int tcp_send_data_with_ack(const char *data, size_t len) {
  if (!data || len == 0 || !tcp_connection || !tcp_connected)
    return -1;

  tcp_config_t *config = get_tcp_config();
  if (!config || len > config->protocol_settings.max_message_size)
    return -1;

  // Generate message ID and create payload with ID
  uint32_t msg_id = generate_message_id();

  // Create JSON payload with message ID
  char *json_with_id = malloc(len + 256);
  if (!json_with_id)
    return -1;

  // Insert message_id into JSON (assumes JSON format)
  if (data[0] == '{') {
    snprintf(json_with_id, len + 256, "{\"message_id\":%u,%s", msg_id,
             data + 1);
  } else {
    snprintf(json_with_id, len + 256, "{\"message_id\":%u,\"data\":\"%s\"}",
             msg_id, data);
  }

  size_t new_len = strlen(json_with_id);
  size_t delimiter_len = strlen(config->protocol_settings.message_delimiter);
  size_t total_len = new_len + delimiter_len;

  char *send_buffer = malloc(total_len);
  if (!send_buffer) {
    free(json_with_id);
    return -1;
  }

  memcpy(send_buffer, json_with_id, new_len);
  memcpy(send_buffer + new_len, config->protocol_settings.message_delimiter,
         delimiter_len);

  // Send data
  size_t sent = mg_send(tcp_connection, send_buffer, total_len);

  // Store message for ACK tracking
  if (sent == total_len) {
    store_pending_message(msg_id, send_buffer, total_len);
#ifdef DEBUG
    printf("TCP: Sent message %u, waiting for ACK\n", msg_id);
#endif
  }

  free(json_with_id);
  free(send_buffer);

  return (sent == total_len) ? 0 : -1;
}

// Original send data function (backward compatibility)
static int tcp_send_data(const char *data, size_t len) {
  if (!data || len == 0 || !tcp_connection || !tcp_connected)
    return -1;

  tcp_config_t *config = get_tcp_config();
  if (!config || len > config->protocol_settings.max_message_size)
    return -1;

  size_t delimiter_len = strlen(config->protocol_settings.message_delimiter);
  size_t total_len = len + delimiter_len;

  char *send_buffer = malloc(total_len);
  if (!send_buffer)
    return -1;

  memcpy(send_buffer, data, len);
  memcpy(send_buffer + len, config->protocol_settings.message_delimiter,
         delimiter_len);

  size_t sent = mg_send(tcp_connection, send_buffer, total_len);
  free(send_buffer);

  return (sent == total_len) ? 0 : -1;
}

// Update received data from external tasks
void tcp_update_received_data(const unsigned char *data, uint16_t data_len) {
  if (!data || data_len == 0)
    return;

  pthread_mutex_lock(&tcp_data_mutex);

  // Store raw data (limit to buffer size)
  size_t copy_len = data_len < sizeof(tcp_last_received_data) - 1
                        ? data_len
                        : sizeof(tcp_last_received_data) - 1;
  memcpy(tcp_last_received_data, data, copy_len);
  tcp_last_received_data[copy_len] = '\0';

  // Update metadata
  tcp_last_received_time = time(NULL);
  tcp_total_messages_received++;

  pthread_mutex_unlock(&tcp_data_mutex);

#ifdef DEBUG
  printf("TCP: Updated received data - %d bytes\n", data_len);
#endif
}

// Clear stored received data
void tcp_clear_received_data(void) {
  pthread_mutex_lock(&tcp_data_mutex);
  memset(tcp_last_received_data, 0, sizeof(tcp_last_received_data));
  tcp_last_received_time = 0;
  tcp_total_messages_received = 0;
  pthread_mutex_unlock(&tcp_data_mutex);
}

// Pong response function
static void tcp_send_pong_response(void) {
  tcp_config_t *config = get_tcp_config();
  if (!config)
    return;

  char pong_msg[256];
  snprintf(pong_msg, sizeof(pong_msg),
           "{\"type\":\"pong\",\"client_id\":\"%s\",\"timestamp\":%ld}",
           config->client_id, time(NULL));

  tcp_send_data(pong_msg, strlen(pong_msg));

#ifdef DEBUG
  printf("TCP: Sent pong response\n");
#endif
}
// Update downlink data from server
static void tcp_update_downlink_data(unsigned char *downlink_buffer,
                                     size_t buffer_size,
                                     const unsigned char *data,
                                     uint16_t data_len) {
  if (!downlink_buffer || !data || data_len == 0 || buffer_size == 0)
    return;

  pthread_mutex_lock(&downlink_mutex);

  // Store raw downlink data (limit to ACTUAL buffer size)
  size_t copy_len = data_len < buffer_size - 1 ? data_len : buffer_size - 1;
  memcpy(downlink_buffer, data, copy_len);
  downlink_buffer[copy_len] = '\0';
  check_downlink = 1; // Indicate new downlink data is available

  pthread_mutex_unlock(&downlink_mutex);

#ifdef DEBUG
  printf("TCP: Stored downlink data - %d bytes\n", (int)copy_len);
#endif
}

// Process received data with ACK handling
static void tcp_process_received_data(const char *data, size_t len) {
  if (!data || len == 0)
    return;

  // Create null-terminated string for processing
  char *msg = malloc(len + 1);
  if (!msg)
    return;
  memcpy(msg, data, len);
  msg[len] = '\0';

  // Check for ACK message
  if (is_ack_message(msg)) {
    uint32_t ack_msg_id = extract_message_id_from_ack(msg);
    if (ack_msg_id > 0) {
      mark_message_acknowledged(ack_msg_id);
    }
    free(msg);
    return;
  }

  // Check for ping message
  if (strcmp(msg, "ping") == 0 || strstr(msg, "\"type\":\"ping\"")) {
    tcp_send_pong_response();
  } else {
    // Store received data for external processing
    tcp_update_downlink_data(downlink_data, sizeof(downlink_data),
                             (const unsigned char *)data, len);

    // Extract message ID from received message and send ACK
    const char *id_start = strstr(msg, "\"message_id\":");
    if (id_start) {
      uint32_t received_msg_id =
          (uint32_t)atol(id_start + strlen("\"message_id\":"));
      if (received_msg_id > 0) {
        send_ack_response(received_msg_id);
      }
    }
  }

  free(msg);

#ifdef DEBUG
  printf("TCP: Processed %zu bytes\n", len);
#endif
}

// Event handler
static void tcp_event_handler(struct mg_connection *c, int ev, void *ev_data) {
  switch (ev) {
  case MG_EV_CONNECT:
    tcp_connected = 1;
    tcp_connection = c;

    tcp_config_t *config = get_tcp_config();
    if (config && config->features.status_reporting) {
      system_info_t *sys_info = get_system_info();
      if (sys_info) {
        char handshake[512];
        snprintf(handshake, sizeof(handshake),
                 "{\"client_id\":\"%s\",\"protocol_version\":\"%s\","
                 "\"gateway_ip\":\"%s\",\"firmware_version\":\"%s\",\"device_"
                 "type\":\"%s\"}%s",
                 config->client_id, config->protocol_version,
                 tcp_get_local_ip(), sys_info->firmware_version,
                 sys_info->device_type,
                 config->protocol_settings.message_delimiter);

        mg_send(c, handshake, strlen(handshake));
      }
    }

#ifdef DEBUG
    printf("TCP: Connected to server\n");
#endif
    break;

  case MG_EV_READ:
    if (c->recv.len > 0) {
      tcp_process_received_data((const char *)c->recv.buf, c->recv.len);
      mg_iobuf_del(&c->recv, 0, c->recv.len);
    }
    break;

  case MG_EV_CLOSE:
  case MG_EV_ERROR:
    tcp_connected = 0;
    tcp_connection = NULL;
#ifdef DEBUG
    printf("TCP: Connection %s\n", ev == MG_EV_CLOSE ? "closed" : "error");
#endif
    break;
  }
}

// Build telemetry payload
static void build_tcp_telemetry_payload(char *payload, size_t payload_size,
                                        time_t timestamp) {
  if (!payload || payload_size == 0)
    return;

  tcp_config_t *config = get_tcp_config();
  if (!config) {
    payload[0] = '\0';
    return;
  }

  char *temp_buffer = malloc(2048);
  if (!temp_buffer) {
    payload[0] = '\0';
    return;
  }

  // Start JSON with timestamp
  if (config->system_fields.include_timestamp) {
    snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);
  } else {
    snprintf(payload, payload_size, "{");
  }

  // Add gateway system information
  system_info_t *sys_info = get_system_info();
  if (sys_info) {
    snprintf(temp_buffer, 2048, ",\"firmware_version\":\"%s\"",
             sys_info->firmware_version);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    snprintf(temp_buffer, 2048, ",\"device_type\":\"%s\"",
             sys_info->device_type);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    snprintf(temp_buffer, 2048, ",\"manufacturer\":\"%s\"",
             sys_info->manufacturer);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    snprintf(temp_buffer, 2048, ",\"model\":\"%s\"", sys_info->model);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
  }

  // Add system information
  if (config->system_fields.include_gateway_ip) {
    snprintf(temp_buffer, 2048, ",\"gateway_ip\":\"%s\"", tcp_get_local_ip());
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
  }

  if (config->system_fields.include_system_info && sys_info) {
    snprintf(temp_buffer, 2048, ",\"status\":\"online\"");
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
  }

  // Add received data information
  pthread_mutex_lock(&tcp_data_mutex);
  if (strlen(tcp_last_received_data) > 0) {
    // Add escaped text data
    char escaped_data[1024];
    escape_json_string(tcp_last_received_data, escaped_data,
                       sizeof(escaped_data));
    snprintf(temp_buffer, 2048, ",\"last_received_data\":\"%s\"", escaped_data);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    // Add hex representation
    char *hex_data =
        data_to_hex_string((const unsigned char *)tcp_last_received_data,
                           strlen(tcp_last_received_data));
    if (hex_data) {
      snprintf(temp_buffer, 2048, ",\"last_received_data_hex\":\"%s\"",
               hex_data);
      strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
      free(hex_data);
    }

    // Add metadata
    snprintf(temp_buffer, 2048, ",\"last_received_time\":%ld",
             tcp_last_received_time);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    snprintf(temp_buffer, 2048, ",\"total_messages_received\":%d",
             tcp_total_messages_received);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
  }
  pthread_mutex_unlock(&tcp_data_mutex);

  // Close JSON object
  strncat(payload, "}", payload_size - strlen(payload) - 1);
  free(temp_buffer);
}

// Main TCP thread function with enhanced acknowledgment support
void *tcp_thread_func(void *arg) {
  // Pause thread if needed
  // Pause thread if needed
  pthread_mutex_lock(&tcp_pause.mutex);
  while (tcp_pause.is_paused) {
    pthread_cond_wait(&tcp_pause.cond, &tcp_pause.mutex);
  }
  pthread_mutex_unlock(&tcp_pause.mutex);
  tcp_config_t *config = get_tcp_config();
  if (!config)
    return NULL;

  // 1. INITIALIZATION
  if (tcp_init() != 0)
    return NULL;

  // 2. INITIAL CONNECTION
  char server_url[256];
  snprintf(server_url, sizeof(server_url), "tcp://%s:%d", config->server_host,
           config->server_port);
  tcp_connect(server_url);

  // 3. WAIT FOR CONNECTION WITH TIMEOUT
  int connection_timeout = config->connection_timeout;
  while (!tcp_connected && connection_timeout > 0) {
#ifdef DEBUG
    printf("TCP: Waiting for connection...\n");
#endif
    mg_mgr_poll(&tcp_mgr, 100);
    connection_timeout--;
  }

  if (!tcp_connected) {
    tcp_close();
    return NULL;
  }

  // 4. ALLOCATE BUFFERS
  char *telemetry_payload = malloc(config->payload_buffer_size);
  if (!telemetry_payload) {
    tcp_close();
    return NULL;
  }

  // 5. MAIN LOOP WITH ACK SUPPORT
  time_t last_publish = 0;
  while (1) {
    // Handle pause/resume
    pthread_mutex_lock(&tcp_pause.mutex);
    while (tcp_pause.is_paused) {
      pthread_cond_wait(&tcp_pause.cond, &tcp_pause.mutex);
    }
    pthread_mutex_unlock(&tcp_pause.mutex);

    // Poll events
    mg_mgr_poll(&tcp_mgr, 100);

    // Check message timeouts and retry
    check_message_timeouts();

    time_t current_time = time(NULL);

    // Get current config (may have been reloaded)
    config = get_tcp_config();
    if (!config) {
#ifdef DEBUG
      printf("Config no longer available, exiting TCP thread\n");
#endif
      break;
    }

    // Send periodic telemetry with ACK support
    if (tcp_connected && current_time - last_publish >= config->send_interval) {
      build_tcp_telemetry_payload(telemetry_payload,
                                  config->payload_buffer_size, current_time);

      if (strlen(telemetry_payload) > 0) {
        // Use enhanced send with ACK tracking
        tcp_send_data_with_ack(telemetry_payload, strlen(telemetry_payload));
        last_publish = current_time;

#ifdef DEBUG
        printf("TCP telemetry sent with ACK support: %s\n", telemetry_payload);
#endif
      }
    }

    // Handle reconnection
    if (!tcp_connected) {
      tcp_reconnect();
      usleep(config->reconnect_delay_ms * 1000);
    } else {
      usleep(config->loop_interval_ms * 1000);
    }
  }

  // 6. CLEANUP
  free(telemetry_payload);
  tcp_close();
  return NULL;
}
