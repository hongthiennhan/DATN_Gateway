#include "mqtt_thread.h"

// Config update tracking
volatile uint8_t config_updated = 0;
pthread_mutex_t config_update_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global Mongoose manager and connection
static struct mg_mgr mqtt_mgr;
static struct mg_connection *mqtt_connection = NULL;
volatile int mqtt_connected = 0;

thread_pause_t mqtt_pause = {.is_paused = false,
                             .mutex = PTHREAD_MUTEX_INITIALIZER,
                             .cond = PTHREAD_COND_INITIALIZER};

// Static buffer for storing received data
static pthread_mutex_t mqtt_data_mutex = PTHREAD_MUTEX_INITIALIZER;
static char mqtt_last_received_data[MAX_RECEIVED_DATA_SIZE] = {0};
static time_t mqtt_last_received_time = 0;
static int mqtt_total_messages_received = 0;

// Safe MQTT config access with mutex protection
mqtt_config_t *safe_get_mqtt_config(void) {
  pthread_mutex_lock(&gateway_config_mutex);
  mqtt_config_t *config = get_mqtt_config();
  pthread_mutex_unlock(&gateway_config_mutex);
  return config;
}

// Get local IP address for gateway identification
char *mqtt_get_local_ip() {
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

// Update received data from external tasks - PUBLIC FUNCTION (no source param)
void mqtt_update_received_data(const unsigned char *data, uint16_t data_len) {
  if (!data || data_len == 0)
    return;

  pthread_mutex_lock(&mqtt_data_mutex);

  // Store raw data (limit to buffer size)
  size_t copy_len = data_len < sizeof(mqtt_last_received_data) - 1
                        ? data_len
                        : sizeof(mqtt_last_received_data) - 1;
  memcpy(mqtt_last_received_data, data, copy_len);
  mqtt_last_received_data[copy_len] = '\0';

  // Update metadata
  mqtt_last_received_time = time(NULL);
  mqtt_total_messages_received++;

  pthread_mutex_unlock(&mqtt_data_mutex);

#ifdef DEBUG
  printf("MQTT: Updated received data - %d bytes\n", data_len);
#endif
}

// Clear stored received data - PUBLIC FUNCTION
void mqtt_clear_received_data(void) {
  pthread_mutex_lock(&mqtt_data_mutex);
  memset(mqtt_last_received_data, 0, sizeof(mqtt_last_received_data));
  mqtt_last_received_time = 0;
  mqtt_total_messages_received = 0;
  pthread_mutex_unlock(&mqtt_data_mutex);
}

// Ensure directory exists for config files
static int ensure_directory_exists(const char *dir) {
  struct stat st;
  if (stat(dir, &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      return 0;
    } else {
#ifdef DEBUG
      fprintf(stderr, "ERROR: %s exists but is not a directory\n", dir);
#endif
      return -1;
    }
  }

  if (mkdir(dir, 0755) == 0) {
#ifdef DEBUG
    printf("Created directory: %s\n", dir);
#endif
    return 0;
  }

  if (errno == EEXIST) {
    return ensure_directory_exists(dir);
  }

#ifdef DEBUG
  fprintf(stderr, "ERROR: Cannot create directory %s: %s\n", dir,
          strerror(errno));
#endif
  return -1;
}

// Validate basic JSON structure
static int validate_json_basic(const void *data, size_t size) {
  if (!data || size == 0) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: Empty JSON data\n");
#endif
    return 0;
  }

  if (size > MAX_JSON_SIZE) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: JSON too large: %zu bytes\n", size);
#endif
    return 0;
  }

  const char *json_str = (const char *)data;
  if (json_str[0] != '{') {
#ifdef DEBUG
    fprintf(stderr, "ERROR: JSON must start with '{'\n");
#endif
    return 0;
  }

  // Find closing brace
  int found_closing = 0;
  for (int i = size - 1; i >= 0; i--) {
    if (json_str[i] == '}') {
      found_closing = 1;
      break;
    } else if (json_str[i] != ' ' && json_str[i] != '\n' &&
               json_str[i] != '\t' && json_str[i] != '\0') {
      break;
    }
  }

  if (!found_closing) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: JSON must end with '}'\n");
#endif
    return 0;
  }

#ifdef DEBUG
  printf("JSON validation passed: %zu bytes\n", size);
#endif
  return 1;
}

// Atomic file write with durability guarantees
static int atomic_write_json_file(const char *dir, const char *filename,
                                  const void *data, size_t size) {
  if (ensure_directory_exists(dir) != 0) {
    return -1;
  }

  char current_path[64], new_path[64];
  snprintf(current_path, sizeof(current_path), "%s/%s", dir, filename);
  snprintf(new_path, sizeof(new_path), "%s/%s.new", dir, filename);

  // Delete old config file if it exists
  if (access(current_path, F_OK) == 0) {
    if (unlink(current_path) != 0) {
#ifdef DEBUG
      fprintf(stderr, "ERROR: Cannot delete old config file %s: %s\n",
              current_path, strerror(errno));
#endif
      return -1;
    }
#ifdef DEBUG
    printf("Deleted old config file: %s\n", current_path);
#endif
  }

  // Create new file
  int fd = open(new_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: Cannot create new file %s: %s\n", new_path,
            strerror(errno));
#endif
    return -1;
  }

  // Write data to new file with partial-write handling
  size_t total_written = 0;
  while (total_written < size) {
    ssize_t written =
        write(fd, (const char *)data + total_written, size - total_written);
    if (written < 0) {
      if (errno == EINTR)
        continue;
#ifdef DEBUG
      fprintf(stderr, "ERROR: Write to new file failed: %s\n", strerror(errno));
#endif
      close(fd);
      unlink(new_path);
      return -1;
    }
    total_written += written;
  }

  // Ensure data is flushed to storage
  if (fsync(fd) != 0) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: fsync new file failed: %s\n", strerror(errno));
#endif
    close(fd);
    unlink(new_path);
    return -1;
  }

  close(fd);

  // Atomically rename new file to current filename
  if (rename(new_path, current_path) != 0) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: rename new file failed: %s\n", strerror(errno));
#endif
    unlink(new_path);
    return -1;
  }

#ifdef DEBUG
  printf("New config file created successfully: %s\n", current_path);
#endif
  return 0;
}

// Update downlink data from server
static void mqtt_update_downlink_data(unsigned char *downlink_buffer,
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
  printf("MQTT: Stored downlink data - %d bytes\n", (int)copy_len);
#endif
}
// MQTT event handler for Mongoose
static void mqtt_event_handler(struct mg_connection *c, int ev, void *ev_data) {
  switch (ev) {
  case MG_EV_MQTT_OPEN:
    // Connected to the broker
    mqtt_connected = 1;
#ifdef DEBUG
    printf("MQTT connected successfully\n");
#endif
    mqtt_config_t *config = safe_get_mqtt_config();
    system_info_t *sys_info = get_system_info();
    if (!config || !sys_info)
      return;

    // Subscribe to control topic
    if (strlen(config->topic_control) > 0) {
      struct mg_mqtt_opts sub_opts = {0};
      sub_opts.topic = mg_str(config->topic_control);
      sub_opts.qos = config->qos;
      mg_mqtt_sub(c, &sub_opts);
#ifdef DEBUG
      printf("Subscribed to control topic: %s\n", config->topic_control);
#endif
    }
#ifdef DEBUG
    printf("Subscribed to RPC requests\n");
#endif

    // Publish device attributes
    char *attributes = malloc(config->attributes_buffer_size);
    if (attributes) {
      snprintf(attributes, config->attributes_buffer_size,
               "{"
               "\"gateway_ip\":\"%s\","
               "\"firmware_version\":\"%s\","
               "\"device_type\":\"%s\","
               "\"manufacturer\":\"%s\","
               "\"model\":\"%s\","
               "\"status\":\"online\""
               "}",
               mqtt_get_local_ip(), sys_info->firmware_version,
               sys_info->device_type, sys_info->manufacturer, sys_info->model);

      struct mg_mqtt_opts pub_opts = {0};
      pub_opts.topic = mg_str(config->topic_attributes);
      pub_opts.message = mg_str(attributes);
      pub_opts.qos = config->qos;
      pub_opts.retain = false;
      mg_mqtt_pub(c, &pub_opts);
      free(attributes);
    }
    break;

  case MG_EV_MQTT_MSG:
    // Received MQTT message event
    struct mg_mqtt_message *mm = (struct mg_mqtt_message *)ev_data;
#ifdef DEBUG
    printf("MQTT message on topic: %.*s\n", (int)mm->topic.len, mm->topic.buf);
#endif
    mqtt_config_t *msg_config = safe_get_mqtt_config();
    if (!msg_config)
      return;

    // Store received data for telemetry (without source param)
    if (mm->data.len > 0) {
      mqtt_update_received_data((const unsigned char *)mm->data.buf,
                                mm->data.len);
    }

    // Copy topic string safely
    char topic_str[256] = {0};
    int topic_len = (mm->topic.len < 255) ? mm->topic.len : 255;
    memcpy(topic_str, mm->topic.buf, topic_len);
    topic_str[topic_len] = '\0';
    if (strlen(msg_config->topic_control) > 0 &&
        strncmp(topic_str, msg_config->topic_control,
                strlen(msg_config->topic_control) - 1) == 0) {
      mqtt_update_downlink_data(downlink_data, sizeof(downlink_data),
                                (const unsigned char *)mm->data.buf,
                                mm->data.len);
    }

    // Check for config update topics
    if ((strlen(msg_config->topic_status) > 0 &&
         strncmp(topic_str, msg_config->topic_status,
                 strlen(msg_config->topic_status) - 1) == 0) ||
        (strlen(msg_config->topic_attributes) > 0 &&
         strncmp(topic_str, msg_config->topic_attributes,
                 strlen(msg_config->topic_attributes)) == 0)) {
#ifdef DEBUG
      printf("Processing config update\n");
#endif
      const void *payload = mm->data.buf;
      size_t payload_size = mm->data.len;

      if (!validate_json_basic(payload, payload_size)) {
#ifdef DEBUG
        fprintf(stderr, "ERROR: JSON validation failed\n");
#endif
        return;
      }

      int result = atomic_write_json_file(CONFIG_DIR, CONFIG_FILE, payload,
                                          payload_size);
      if (result == 0) {
#ifdef DEBUG
        printf("Config updated from server\n");
#endif
        if (pthread_mutex_trylock(&config_update_mutex) == 0) {
          config_updated = 1;
          pthread_mutex_unlock(&config_update_mutex);
        }
      }
    }
    break;

  case MG_EV_CLOSE:
    // Connection closed
    mqtt_connected = 0;
    mqtt_connection = NULL;
#ifdef DEBUG
    printf("MQTT disconnected\n");
#endif
    break;

  case MG_EV_ERROR:
    // Connection error
    mqtt_connected = 0;
    mqtt_connection = NULL;
#ifdef DEBUG
    printf("MQTT connection error\n");
#endif
    break;
  }
}

// Check and reload config if updated
static int check_and_reload_config(void) {
  pthread_mutex_lock(&config_update_mutex);
  int should_reload = config_updated;
  if (should_reload) {
    config_updated = 0;
  }
  pthread_mutex_unlock(&config_update_mutex);

  if (!should_reload)
    return 0;

#ifdef DEBUG
  printf("Reloading config from server update\n");
#endif
  cleanup_gateway_config();
  char config_path[64];
  snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR, CONFIG_FILE);

  int result = 0;
  if (load_gateway_config(config_path) == 0) {
#ifdef DEBUG
    printf("Config reloaded successfully\n");
#endif
    result = 1;
  } else {
#ifdef DEBUG
    fprintf(stderr, "Failed to reload config, using fallback\n");
#endif
    // Try fallback configs
    snprintf(config_path, sizeof(config_path), "%s/%s", CONFIG_DIR,
             FALLBACK_CONFIG_FILE);
    load_gateway_config(config_path);
  }
  return result;
}

// Request config from server
static int request_config_json_robust(void) {
  mqtt_config_t *cfg = safe_get_mqtt_config();
  if (!cfg || !mqtt_connection || !mqtt_connected) {
#ifdef DEBUG
    fprintf(stderr, "ERROR: Cannot request config - MQTT not ready\n");
#endif
    return 0;
  }

#ifdef DEBUG
  printf("Requesting config from server\n");
#endif

  // Subscribe to config topics
  struct mg_mqtt_opts attr_resp_opts = {0};
  attr_resp_opts.topic = mg_str("v1/devices/me/attributes/response/+");
  attr_resp_opts.qos = cfg->qos;
  mg_mqtt_sub(mqtt_connection, &attr_resp_opts);

  struct mg_mqtt_opts attr_opts = {0};
  attr_opts.topic = mg_str("v1/devices/me/attributes");
  attr_opts.qos = cfg->qos;
  mg_mqtt_sub(mqtt_connection, &attr_opts);

  // Publish config request
  const char *request_payload = "{\"sharedKeys\":\"config\"}";
  struct mg_mqtt_opts pub_opts = {0};
  pub_opts.topic = mg_str("v1/devices/me/attributes/request/1");
  pub_opts.message = mg_str(request_payload);
  pub_opts.qos = cfg->qos;
  pub_opts.retain = false;
  mg_mqtt_pub(mqtt_connection, &pub_opts);

#ifdef DEBUG
  printf("Config request sent\n");
#endif
  return 1;
}

// Build telemetry payload with gateway system data and received data
static void build_telemetry_payload(char *payload, size_t payload_size,
                                    time_t timestamp) {
  mqtt_config_t *config = safe_get_mqtt_config();
  system_info_t *sys_info = get_system_info();
  if (!config || !sys_info)
    return;

  char *temp_buffer = malloc(2048);
  if (!temp_buffer)
    return;

  // Start JSON with timestamp
  if (config->system_fields.include_timestamp) {
    snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);
  } else {
    snprintf(payload, payload_size, "{");
  }

  // Add gateway system information
  snprintf(temp_buffer, 2048, ",\"firmware_version\":\"%s\"",
           sys_info->firmware_version);
  strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

  snprintf(temp_buffer, 2048, ",\"device_type\":\"%s\"", sys_info->device_type);
  strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

  snprintf(temp_buffer, 2048, ",\"manufacturer\":\"%s\"",
           sys_info->manufacturer);
  strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

  snprintf(temp_buffer, 2048, ",\"model\":\"%s\"", sys_info->model);
  strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

  // Add system info
  if (config->system_fields.include_gateway_ip) {
    snprintf(temp_buffer, 2048, ",\"gateway_ip\":\"%s\"", mqtt_get_local_ip());
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
  }

  // Add status
  snprintf(temp_buffer, 2048, ",\"status\":\"online\"");
  strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

  // Add received data information (no source tracking)
  pthread_mutex_lock(&mqtt_data_mutex);
  if (strlen(mqtt_last_received_data) > 0) {
    // Add escaped text data
    char escaped_data[1024];
    escape_json_string(mqtt_last_received_data, escaped_data,
                       sizeof(escaped_data));
    snprintf(temp_buffer, 2048, ",\"last_received_data\":\"%s\"", escaped_data);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    // Add hex representation
    char *hex_data =
        data_to_hex_string((const unsigned char *)mqtt_last_received_data,
                           strlen(mqtt_last_received_data));
    if (hex_data) {
      snprintf(temp_buffer, 2048, ",\"last_received_data_hex\":\"%s\"",
               hex_data);
      strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
      free(hex_data);
    }

    // Add metadata
    snprintf(temp_buffer, 2048, ",\"last_received_time\":%ld",
             mqtt_last_received_time);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);

    snprintf(temp_buffer, 2048, ",\"total_messages_received\":%d",
             mqtt_total_messages_received);
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
  }
  pthread_mutex_unlock(&mqtt_data_mutex);

  strncat(payload, "}", payload_size - strlen(payload) - 1);
  free(temp_buffer);
}

// MQTT main thread function
void *mqtt_thread_func(void *arg) {
  // Pause thread if needed
  pthread_mutex_lock(&mqtt_pause.mutex);
  while (mqtt_pause.is_paused) {
    pthread_cond_wait(&mqtt_pause.cond, &mqtt_pause.mutex);
  }
  pthread_mutex_unlock(&mqtt_pause.mutex);
  mqtt_config_t *config = safe_get_mqtt_config();
  if (!config) {
#ifdef DEBUG
    printf("No MQTT config available\n");
#endif
    return NULL;
  }

  // Init Mongoose manager
  mg_mgr_init(&mqtt_mgr);

  // Build Connection URL
  char url[512];
  if (strlen(config->username) > 0) {
    snprintf(url, sizeof(url), "mqtt://%s@%s:%d", config->username,
             config->broker_host, config->broker_port);
  } else {
    snprintf(url, sizeof(url), "mqtt://%s:%d", config->broker_host,
             config->broker_port);
  }

#ifdef DEBUG
  printf("Connecting to MQTT broker: %s\n", url);
  printf("DEBUG: Username: '%s'\n", config->username);
  printf("DEBUG: Password: '%s'\n", config->password);
#endif

  struct mg_mqtt_opts opts = {
      .client_id = mg_str(config->client_id),
      .user = mg_str(config->username), // Explicit username
      .pass = mg_str(config->password), // Use configured password
      .clean = true,                    // Clean session
      .keepalive = 60,                  // Keep alive timeout
      .version = 4                      // MQTT 3.1.1
  };

  // Connect to MQTT
  mqtt_connection =
      mg_mqtt_connect(&mqtt_mgr, url, &opts, mqtt_event_handler, NULL);
  if (!mqtt_connection) {
#ifdef DEBUG
    printf("Failed to create MQTT connection\n");
#endif
    mg_mgr_free(&mqtt_mgr);
    return NULL;
  }

  // Wait for connection
  int connection_timeout = config->connection_timeout;
  while (!mqtt_connected && connection_timeout > 0) {
    mg_mgr_poll(&mqtt_mgr, 100); // 100 ms poll
    connection_timeout--;
    if (connection_timeout % 10 == 0 && connection_timeout > 0) {
#ifdef DEBUG
      printf("MQTT: Waiting for connection... (%d seconds left)\n",
             connection_timeout / 10);
#endif
    }
  }

  if (!mqtt_connected) {
#ifdef DEBUG
    printf("MQTT connection timeout\n");
#endif
    mg_mgr_free(&mqtt_mgr);
    return NULL;
  }

  // Request initial config
#ifdef DEBUG
  printf("Requesting initial config\n");
#endif
  if (request_config_json_robust()) {
    sleep(3); // Wait for response
  }

  // Check for config updates
  check_and_reload_config();

  // Allocate buffers
  time_t last_publish = 0, last_status = 0;
  char *telemetry_payload = malloc(config->payload_buffer_size);
  char *status_payload = malloc(config->payload_buffer_size);

  if (!telemetry_payload || !status_payload) {
#ifdef DEBUG
    printf("Memory allocation failed\n");
#endif
    if (telemetry_payload)
      free(telemetry_payload);
    if (status_payload)
      free(status_payload);
    mg_mgr_free(&mqtt_mgr);
    return NULL;
  }

  while (1) {
    // Pause thread if needed
    pthread_mutex_lock(&mqtt_pause.mutex);
    while (mqtt_pause.is_paused) {
      pthread_cond_wait(&mqtt_pause.cond, &mqtt_pause.mutex);
    }
    pthread_mutex_unlock(&mqtt_pause.mutex);

    // Poll events
    mg_mgr_poll(&mqtt_mgr, 100);

    time_t current_time = time(NULL);

    // Reload config if updated
    check_and_reload_config();

    // Get current config (may have been reloaded)
    config = safe_get_mqtt_config();
    if (!config) {
#ifdef DEBUG
      printf("Config no longer available, exiting MQTT thread\n");
#endif
      break;
    }

    // Publish telemetry at interval
    if (current_time - last_publish >= config->publish_interval) {
      build_telemetry_payload(telemetry_payload, config->payload_buffer_size,
                              current_time);
      if (mqtt_connection && mqtt_connected) {
        struct mg_mqtt_opts pub_opts = {0};
        pub_opts.topic = mg_str(config->topic_telemetry);
        pub_opts.message = mg_str(telemetry_payload);
        pub_opts.qos = config->qos;
        pub_opts.retain = false;
        mg_mqtt_pub(mqtt_connection, &pub_opts);
        last_publish = current_time;
#ifdef DEBUG
        printf("Telemetry published: %s\n", telemetry_payload);
#endif
      }
    }

    // Send status update every 60 seconds
    if (strlen(config->topic_status) > 0 &&
        (current_time - last_status) >= 60) {
      snprintf(
          status_payload, config->payload_buffer_size,
          "{\"status\":\"online\",\"timestamp\":%ld000,\"gateway_ip\":\"%s\"}",
          current_time, mqtt_get_local_ip());
      if (mqtt_connection && mqtt_connected) {
        struct mg_mqtt_opts pub_opts = {0};
        pub_opts.topic = mg_str(config->topic_status);
        pub_opts.message = mg_str(status_payload);
        pub_opts.qos = config->qos;
        pub_opts.retain = false;
        mg_mqtt_pub(mqtt_connection, &pub_opts);
        last_status = current_time;
      }
    }

    if (!mqtt_connected) {
#ifdef DEBUG
      printf("MQTT: Connection lost, attempting reconnect...\n");
#endif
      // Clear existing connection
      if (mqtt_connection) {
        mg_mgr_poll(&mqtt_mgr, 0); // Process any pending events
        mqtt_connection = NULL;
      }

      // Create new connection
      mqtt_connection =
          mg_mqtt_connect(&mqtt_mgr, url, &opts, mqtt_event_handler, NULL);
      // Wait before next attempt
      usleep(config->reconnect_delay_ms * 1000);
    }

    usleep(config->loop_interval_ms * 1000);
  }

  // Cleanup
  free(telemetry_payload);
  free(status_payload);
  mg_mgr_free(&mqtt_mgr);
  return NULL;
}
