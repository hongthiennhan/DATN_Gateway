#include "gateway_config.h"

// Global gateway configuration instance
static gateway_config_t gateway_config = {0};

// Global configuration protection mutex
pthread_mutex_t gateway_config_mutex = PTHREAD_MUTEX_INITIALIZER;

// Safe string copy with null termination
static void safe_strncpy(char *dest, const char *src, size_t dest_size) {
  if (!dest || !src || dest_size == 0)
    return;

  size_t len = strlen(src);
  if (len >= dest_size)
    len = dest_size - 1;

  memcpy(dest, src, len);
  dest[len] = '\0';
}

// Parse baudrate array from JSON
static int parse_baudrate_array(json_object *baudrate_array,
                                baudrate_mapping_t **baudrates,
                                uint8_t *count) {
  if (!baudrate_array || !baudrates || !count)
    return -1;

  uint32_t array_len = json_object_array_length(baudrate_array);
  if (array_len <= 0)
    return 0;

  *baudrates = malloc(sizeof(baudrate_mapping_t) * array_len);
  if (!*baudrates)
    return -1;

  *count = 0;
  for (uint32_t i = 0; i < array_len; i++) {
    json_object *baudrate_obj = json_object_array_get_idx(baudrate_array, i);
    json_object *rate_obj, *code_obj;

    if (json_object_object_get_ex(baudrate_obj, "rate", &rate_obj) &&
        json_object_object_get_ex(baudrate_obj, "speed_code", &code_obj)) {

      baudrate_mapping_t *mapping = &(*baudrates)[*count];
      mapping->rate = json_object_get_int(rate_obj);
      safe_strncpy(mapping->speed_code, json_object_get_string(code_obj),
                   sizeof(mapping->speed_code));
      (*count)++;
    }
  }

  return 0;
}

// Load gateway configuration from JSON file
int load_gateway_config(const char *config_file) {
  if (!config_file)
    return -1;

  printf("Loading gateway configuration from: %s\n", config_file);

  pthread_mutex_lock(&gateway_config_mutex);

  json_object *root = json_object_from_file(config_file);
  if (!root) {
    printf("Error: Cannot load config file %s\n", config_file);
    pthread_mutex_unlock(&gateway_config_mutex);
    return -1;
  }

  json_object *shared = NULL;
  json_object *config = NULL;

  if (jsonobject_object_get_ex(root, "shared", &shared)) {
    if (jsonobject_object_get_ex(root, "config", &config)) {
      root = config;
    }
  }

  // Initialize configuration structure
  memset(&gateway_config, 0, sizeof(gateway_config_t));

  // Parse system configuration
  json_object *system_config_obj = NULL;
  if (json_object_object_get_ex(root, "system_config", &system_config_obj)) {
    json_object *temp_obj;

    if (json_object_object_get_ex(system_config_obj, "uart_wait_timeout",
                                  &temp_obj))
      gateway_config.system_config.uart_wait_timeout =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(system_config_obj, "default_baudrate",
                                  &temp_obj))
      gateway_config.system_config.default_baudrate =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(system_config_obj, "default_device",
                                  &temp_obj))
      safe_strncpy(gateway_config.system_config.default_device,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.system_config.default_device));
    if (json_object_object_get_ex(system_config_obj, "startup_clear_duration",
                                  &temp_obj))
      gateway_config.system_config.startup_clear_duration =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(system_config_obj, "server_com_type",
                                  &temp_obj))
      safe_strncpy(gateway_config.system_config.server_com_type,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.system_config.server_com_type));
  }

  // Parse system info
  json_object *system_info_obj = NULL;
  if (json_object_object_get_ex(root, "system_info", &system_info_obj)) {
    json_object *temp_obj;

    if (json_object_object_get_ex(system_info_obj, "firmware_version",
                                  &temp_obj))
      safe_strncpy(gateway_config.system_info.firmware_version,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.system_info.firmware_version));
    if (json_object_object_get_ex(system_info_obj, "device_type", &temp_obj))
      safe_strncpy(gateway_config.system_info.device_type,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.system_info.device_type));
    if (json_object_object_get_ex(system_info_obj, "manufacturer", &temp_obj))
      safe_strncpy(gateway_config.system_info.manufacturer,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.system_info.manufacturer));
    if (json_object_object_get_ex(system_info_obj, "model", &temp_obj))
      safe_strncpy(gateway_config.system_info.model,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.system_info.model));
  }

  // Parse UART configuration
  json_object *uart_obj = NULL;
  if (json_object_object_get_ex(root, "uart_config", &uart_obj)) {
    json_object *temp_obj;

    // Parse buffer sizes
    json_object *buffer_sizes_obj;
    if (json_object_object_get_ex(uart_obj, "buffer_sizes",
                                  &buffer_sizes_obj)) {
      if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer",
                                    &temp_obj))
        gateway_config.uart_config.response_buffer_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
        gateway_config.uart_config.temp_buffer_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer",
                                    &temp_obj))
        gateway_config.uart_config.error_message_buffer_size =
            json_object_get_int(temp_obj);
    }

    // Parse timing
    json_object *timing_obj;
    if (json_object_object_get_ex(uart_obj, "timing", &timing_obj)) {
      if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &temp_obj))
        gateway_config.uart_config.poll_interval_ms =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(timing_obj, "flush_interval_ms", &temp_obj))
        gateway_config.uart_config.flush_interval_ms =
            json_object_get_int(temp_obj);
    }

    // Parse supported baudrates
    json_object *baudrate_array;
    if (json_object_object_get_ex(uart_obj, "supported_baudrates",
                                  &baudrate_array)) {
      parse_baudrate_array(baudrate_array,
                           &gateway_config.uart_config.supported_baudrates,
                           &gateway_config.uart_config.baudrate_count);
    }

    if (json_object_object_get_ex(uart_obj, "default_baudrate_fallback",
                                  &temp_obj))
      gateway_config.uart_config.default_baudrate_fallback =
          json_object_get_int(temp_obj);
  }

  // Parse Modbus configuration (similar to UART)
  json_object *modbus_obj = NULL;
  if (json_object_object_get_ex(root, "modbus_config", &modbus_obj)) {
    json_object *temp_obj;

    json_object *buffer_sizes_obj;
    if (json_object_object_get_ex(modbus_obj, "buffer_sizes",
                                  &buffer_sizes_obj)) {
      if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer",
                                    &temp_obj))
        gateway_config.modbus_config.response_buffer_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
        gateway_config.modbus_config.temp_buffer_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer",
                                    &temp_obj))
        gateway_config.modbus_config.error_message_buffer_size =
            json_object_get_int(temp_obj);
    }

    json_object *timing_obj;
    if (json_object_object_get_ex(modbus_obj, "timing", &timing_obj)) {
      if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &temp_obj))
        gateway_config.modbus_config.poll_interval_ms =
            json_object_get_int(temp_obj);
    }

    json_object *baudrate_array;
    if (json_object_object_get_ex(modbus_obj, "supported_baudrates",
                                  &baudrate_array)) {
      parse_baudrate_array(baudrate_array,
                           &gateway_config.modbus_config.supported_baudrates,
                           &gateway_config.modbus_config.baudrate_count);
    }

    if (json_object_object_get_ex(modbus_obj, "default_baudrate_fallback",
                                  &temp_obj))
      gateway_config.modbus_config.default_baudrate_fallback =
          json_object_get_int(temp_obj);
  }

  // Parse CAN configuration (similar to UART)
  json_object *can_obj = NULL;
  if (json_object_object_get_ex(root, "can_config", &can_obj)) {
    json_object *temp_obj;

    json_object *buffer_sizes_obj;
    if (json_object_object_get_ex(can_obj, "buffer_sizes", &buffer_sizes_obj)) {
      if (json_object_object_get_ex(buffer_sizes_obj, "response_buffer",
                                    &temp_obj))
        gateway_config.can_config.response_buffer_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(buffer_sizes_obj, "temp_buffer", &temp_obj))
        gateway_config.can_config.temp_buffer_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(buffer_sizes_obj, "error_message_buffer",
                                    &temp_obj))
        gateway_config.can_config.error_message_buffer_size =
            json_object_get_int(temp_obj);
    }

    json_object *timing_obj;
    if (json_object_object_get_ex(can_obj, "timing", &timing_obj)) {
      if (json_object_object_get_ex(timing_obj, "poll_interval_ms", &temp_obj))
        gateway_config.can_config.poll_interval_ms =
            json_object_get_int(temp_obj);
    }

    json_object *baudrate_array;
    if (json_object_object_get_ex(can_obj, "supported_baudrates",
                                  &baudrate_array)) {
      parse_baudrate_array(baudrate_array,
                           &gateway_config.can_config.supported_baudrates,
                           &gateway_config.can_config.baudrate_count);
    }

    if (json_object_object_get_ex(can_obj, "default_baudrate_fallback",
                                  &temp_obj))
      gateway_config.can_config.default_baudrate_fallback =
          json_object_get_int(temp_obj);
  }

  // Parse MQTT configuration
  json_object *mqtt_obj = NULL;
  if (json_object_object_get_ex(root, "mqtt_config", &mqtt_obj)) {
    json_object *temp_obj;

    if (json_object_object_get_ex(mqtt_obj, "broker_host", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.broker_host,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.broker_host));
    if (json_object_object_get_ex(mqtt_obj, "broker_port", &temp_obj))
      gateway_config.mqtt_config.broker_port = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "client_id", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.client_id,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.client_id));
    if (json_object_object_get_ex(mqtt_obj, "username", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.username,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.username));
    if (json_object_object_get_ex(mqtt_obj, "password", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.password,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.password));
    if (json_object_object_get_ex(mqtt_obj, "topic_telemetry", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.topic_telemetry,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.topic_telemetry));
    if (json_object_object_get_ex(mqtt_obj, "topic_attributes", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.topic_attributes,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.topic_attributes));
    if (json_object_object_get_ex(mqtt_obj, "topic_control", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.topic_control,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.topic_control));
    if (json_object_object_get_ex(mqtt_obj, "topic_status", &temp_obj))
      safe_strncpy(gateway_config.mqtt_config.topic_status,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.mqtt_config.topic_status));
    if (json_object_object_get_ex(mqtt_obj, "qos", &temp_obj))
      gateway_config.mqtt_config.qos = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "publish_interval", &temp_obj))
      gateway_config.mqtt_config.publish_interval =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "connection_timeout", &temp_obj))
      gateway_config.mqtt_config.connection_timeout =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "reconnect_delay_ms", &temp_obj))
      gateway_config.mqtt_config.reconnect_delay_ms =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "loop_interval_ms", &temp_obj))
      gateway_config.mqtt_config.loop_interval_ms =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "payload_buffer_size", &temp_obj))
      gateway_config.mqtt_config.payload_buffer_size =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(mqtt_obj, "attributes_buffer_size",
                                  &temp_obj))
      gateway_config.mqtt_config.attributes_buffer_size =
          json_object_get_int(temp_obj);

    // Parse system fields
    json_object *system_fields_obj;
    if (json_object_object_get_ex(mqtt_obj, "system_fields",
                                  &system_fields_obj)) {
      if (json_object_object_get_ex(system_fields_obj, "data_source",
                                    &temp_obj))
        safe_strncpy(
            gateway_config.mqtt_config.system_fields.data_source,
            json_object_get_string(temp_obj),
            sizeof(gateway_config.mqtt_config.system_fields.data_source));
      if (json_object_object_get_ex(system_fields_obj, "include_timestamp",
                                    &temp_obj))
        gateway_config.mqtt_config.system_fields.include_timestamp =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_gateway_ip",
                                    &temp_obj))
        gateway_config.mqtt_config.system_fields.include_gateway_ip =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_node_count",
                                    &temp_obj))
        gateway_config.mqtt_config.system_fields.include_node_count =
            json_object_get_boolean(temp_obj);
    }
  }

  // Parse TCP configuration
  json_object *tcp_obj = NULL;
  if (json_object_object_get_ex(root, "tcp_config", &tcp_obj)) {
    json_object *temp_obj;

    if (json_object_object_get_ex(tcp_obj, "server_host", &temp_obj))
      safe_strncpy(gateway_config.tcp_config.server_host,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.tcp_config.server_host));
    if (json_object_object_get_ex(tcp_obj, "server_port", &temp_obj))
      gateway_config.tcp_config.server_port = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "config_server_host", &temp_obj))
      safe_strncpy(gateway_config.tcp_config.config_server_host,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.tcp_config.config_server_host));
    if (json_object_object_get_ex(tcp_obj, "config_server_port", &temp_obj))
      gateway_config.tcp_config.config_server_port =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "client_id", &temp_obj))
      safe_strncpy(gateway_config.tcp_config.client_id,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.tcp_config.client_id));
    if (json_object_object_get_ex(tcp_obj, "protocol_version", &temp_obj))
      safe_strncpy(gateway_config.tcp_config.protocol_version,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.tcp_config.protocol_version));
    if (json_object_object_get_ex(tcp_obj, "data_format", &temp_obj))
      safe_strncpy(gateway_config.tcp_config.data_format,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.tcp_config.data_format));
    if (json_object_object_get_ex(tcp_obj, "send_interval", &temp_obj))
      gateway_config.tcp_config.send_interval = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "connection_timeout", &temp_obj))
      gateway_config.tcp_config.connection_timeout =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "reconnect_delay_ms", &temp_obj))
      gateway_config.tcp_config.reconnect_delay_ms =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "loop_interval_ms", &temp_obj))
      gateway_config.tcp_config.loop_interval_ms =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "payload_buffer_size", &temp_obj))
      gateway_config.tcp_config.payload_buffer_size =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(tcp_obj, "receive_buffer_size", &temp_obj))
      gateway_config.tcp_config.receive_buffer_size =
          json_object_get_int(temp_obj);

    // Parse socket options
    json_object *socket_options_obj;
    if (json_object_object_get_ex(tcp_obj, "socket_options",
                                  &socket_options_obj)) {
      if (json_object_object_get_ex(socket_options_obj, "keepalive", &temp_obj))
        gateway_config.tcp_config.socket_options.keepalive =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "keepalive_idle",
                                    &temp_obj))
        gateway_config.tcp_config.socket_options.keepalive_idle =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "keepalive_interval",
                                    &temp_obj))
        gateway_config.tcp_config.socket_options.keepalive_interval =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "keepalive_count",
                                    &temp_obj))
        gateway_config.tcp_config.socket_options.keepalive_count =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "tcp_nodelay",
                                    &temp_obj))
        gateway_config.tcp_config.socket_options.tcp_nodelay =
            json_object_get_boolean(temp_obj);
    }

    // Parse system fields
    json_object *system_fields_obj;
    if (json_object_object_get_ex(tcp_obj, "system_fields",
                                  &system_fields_obj)) {
      if (json_object_object_get_ex(system_fields_obj, "data_source",
                                    &temp_obj))
        safe_strncpy(
            gateway_config.tcp_config.system_fields.data_source,
            json_object_get_string(temp_obj),
            sizeof(gateway_config.tcp_config.system_fields.data_source));
      if (json_object_object_get_ex(system_fields_obj, "include_timestamp",
                                    &temp_obj))
        gateway_config.tcp_config.system_fields.include_timestamp =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_gateway_ip",
                                    &temp_obj))
        gateway_config.tcp_config.system_fields.include_gateway_ip =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_node_count",
                                    &temp_obj))
        gateway_config.tcp_config.system_fields.include_node_count =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_system_info",
                                    &temp_obj))
        gateway_config.tcp_config.system_fields.include_system_info =
            json_object_get_boolean(temp_obj);
    }

    // Parse protocol settings
    json_object *protocol_settings_obj;
    if (json_object_object_get_ex(tcp_obj, "protocol_settings",
                                  &protocol_settings_obj)) {
      if (json_object_object_get_ex(protocol_settings_obj, "message_delimiter",
                                    &temp_obj))
        safe_strncpy(
            gateway_config.tcp_config.protocol_settings.message_delimiter,
            json_object_get_string(temp_obj),
            sizeof(
                gateway_config.tcp_config.protocol_settings.message_delimiter));
      if (json_object_object_get_ex(protocol_settings_obj, "max_message_size",
                                    &temp_obj))
        gateway_config.tcp_config.protocol_settings.max_message_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(protocol_settings_obj,
                                    "compression_enabled", &temp_obj))
        gateway_config.tcp_config.protocol_settings.compression_enabled =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(protocol_settings_obj, "encryption_enabled",
                                    &temp_obj))
        gateway_config.tcp_config.protocol_settings.encryption_enabled =
            json_object_get_boolean(temp_obj);
    }

    // Parse features
    json_object *features_obj;
    if (json_object_object_get_ex(tcp_obj, "features", &features_obj)) {
      if (json_object_object_get_ex(features_obj, "config_download", &temp_obj))
        gateway_config.tcp_config.features.config_download =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "control_commands",
                                    &temp_obj))
        gateway_config.tcp_config.features.control_commands =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "telemetry_upload",
                                    &temp_obj))
        gateway_config.tcp_config.features.telemetry_upload =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "status_reporting",
                                    &temp_obj))
        gateway_config.tcp_config.features.status_reporting =
            json_object_get_boolean(temp_obj);
    }
  }

  // Parse UDP configuration
  json_object *udp_obj = NULL;
  if (json_object_object_get_ex(root, "udp_config", &udp_obj)) {
    json_object *temp_obj;

    if (json_object_object_get_ex(udp_obj, "server_host", &temp_obj))
      safe_strncpy(gateway_config.udp_config.server_host,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.udp_config.server_host));
    if (json_object_object_get_ex(udp_obj, "server_port", &temp_obj))
      gateway_config.udp_config.server_port = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(udp_obj, "client_id", &temp_obj))
      safe_strncpy(gateway_config.udp_config.client_id,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.udp_config.client_id));
    if (json_object_object_get_ex(udp_obj, "protocol_version", &temp_obj))
      safe_strncpy(gateway_config.udp_config.protocol_version,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.udp_config.protocol_version));
    if (json_object_object_get_ex(udp_obj, "data_format", &temp_obj))
      safe_strncpy(gateway_config.udp_config.data_format,
                   json_object_get_string(temp_obj),
                   sizeof(gateway_config.udp_config.data_format));
    if (json_object_object_get_ex(udp_obj, "send_interval", &temp_obj))
      gateway_config.udp_config.send_interval = json_object_get_int(temp_obj);
    if (json_object_object_get_ex(udp_obj, "connection_timeout", &temp_obj))
      gateway_config.udp_config.connection_timeout =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(udp_obj, "reconnect_delay_ms", &temp_obj))
      gateway_config.udp_config.reconnect_delay_ms =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(udp_obj, "loop_interval_ms", &temp_obj))
      gateway_config.udp_config.loop_interval_ms =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(udp_obj, "payload_buffer_size", &temp_obj))
      gateway_config.udp_config.payload_buffer_size =
          json_object_get_int(temp_obj);
    if (json_object_object_get_ex(udp_obj, "receive_buffer_size", &temp_obj))
      gateway_config.udp_config.receive_buffer_size =
          json_object_get_int(temp_obj);

    // Parse socket options
    json_object *socket_options_obj;
    if (json_object_object_get_ex(udp_obj, "socket_options",
                                  &socket_options_obj)) {
      if (json_object_object_get_ex(socket_options_obj, "broadcast", &temp_obj))
        gateway_config.udp_config.socket_options.broadcast =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "reuse_addr",
                                    &temp_obj))
        gateway_config.udp_config.socket_options.reuse_addr =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "reuse_port",
                                    &temp_obj))
        gateway_config.udp_config.socket_options.reuse_port =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "receive_timeout_ms",
                                    &temp_obj))
        gateway_config.udp_config.socket_options.receive_timeout_ms =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(socket_options_obj, "send_timeout_ms",
                                    &temp_obj))
        gateway_config.udp_config.socket_options.send_timeout_ms =
            json_object_get_int(temp_obj);
    }

    // Parse system fields
    json_object *system_fields_obj;
    if (json_object_object_get_ex(udp_obj, "system_fields",
                                  &system_fields_obj)) {
      if (json_object_object_get_ex(system_fields_obj, "data_source",
                                    &temp_obj))
        safe_strncpy(
            gateway_config.udp_config.system_fields.data_source,
            json_object_get_string(temp_obj),
            sizeof(gateway_config.udp_config.system_fields.data_source));
      if (json_object_object_get_ex(system_fields_obj, "include_timestamp",
                                    &temp_obj))
        gateway_config.udp_config.system_fields.include_timestamp =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_gateway_ip",
                                    &temp_obj))
        gateway_config.udp_config.system_fields.include_gateway_ip =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_node_count",
                                    &temp_obj))
        gateway_config.udp_config.system_fields.include_node_count =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(system_fields_obj, "include_system_info",
                                    &temp_obj))
        gateway_config.udp_config.system_fields.include_system_info =
            json_object_get_boolean(temp_obj);
    }

    // Parse protocol settings
    json_object *protocol_settings_obj;
    if (json_object_object_get_ex(udp_obj, "protocol_settings",
                                  &protocol_settings_obj)) {
      if (json_object_object_get_ex(protocol_settings_obj, "message_delimiter",
                                    &temp_obj))
        safe_strncpy(
            gateway_config.udp_config.protocol_settings.message_delimiter,
            json_object_get_string(temp_obj),
            sizeof(
                gateway_config.udp_config.protocol_settings.message_delimiter));
      if (json_object_object_get_ex(protocol_settings_obj, "max_message_size",
                                    &temp_obj))
        gateway_config.udp_config.protocol_settings.max_message_size =
            json_object_get_int(temp_obj);
      if (json_object_object_get_ex(protocol_settings_obj,
                                    "compression_enabled", &temp_obj))
        gateway_config.udp_config.protocol_settings.compression_enabled =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(protocol_settings_obj, "encryption_enabled",
                                    &temp_obj))
        gateway_config.udp_config.protocol_settings.encryption_enabled =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(protocol_settings_obj, "checksum_enabled",
                                    &temp_obj))
        gateway_config.udp_config.protocol_settings.checksum_enabled =
            json_object_get_boolean(temp_obj);
    }

    // Parse features
    json_object *features_obj;
    if (json_object_object_get_ex(udp_obj, "features", &features_obj)) {
      if (json_object_object_get_ex(features_obj, "control_commands",
                                    &temp_obj))
        gateway_config.udp_config.features.control_commands =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "telemetry_upload",
                                    &temp_obj))
        gateway_config.udp_config.features.telemetry_upload =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "status_reporting",
                                    &temp_obj))
        gateway_config.udp_config.features.status_reporting =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "broadcast_discovery",
                                    &temp_obj))
        gateway_config.udp_config.features.broadcast_discovery =
            json_object_get_boolean(temp_obj);
      if (json_object_object_get_ex(features_obj, "multicast_support",
                                    &temp_obj))
        gateway_config.udp_config.features.multicast_support =
            json_object_get_boolean(temp_obj);
    }
  }

  json_object_put(root);
  pthread_mutex_unlock(&gateway_config_mutex);

  printf("Gateway configuration loaded successfully\n");
  return 0;
}

// Cleanup gateway configuration resources
void cleanup_gateway_config(void) {
  pthread_mutex_lock(&gateway_config_mutex);

  // Free dynamic memory
  if (gateway_config.uart_config.supported_baudrates) {
    free(gateway_config.uart_config.supported_baudrates);
    gateway_config.uart_config.supported_baudrates = NULL;
  }

  if (gateway_config.modbus_config.supported_baudrates) {
    free(gateway_config.modbus_config.supported_baudrates);
    gateway_config.modbus_config.supported_baudrates = NULL;
  }

  if (gateway_config.can_config.supported_baudrates) {
    free(gateway_config.can_config.supported_baudrates);
    gateway_config.can_config.supported_baudrates = NULL;
  }

  memset(&gateway_config, 0, sizeof(gateway_config_t));
  pthread_mutex_unlock(&gateway_config_mutex);

  printf("Gateway configuration cleanup completed\n");
}

// Thread-safe getter functions
system_config_t *get_system_config(void) {
  return &gateway_config.system_config;
}

system_info_t *get_system_info(void) { return &gateway_config.system_info; }

uart_config_t *get_uart_config(void) { return &gateway_config.uart_config; }

modbus_config_t *get_modbus_config(void) {
  return &gateway_config.modbus_config;
}

can_config_t *get_can_config(void) { return &gateway_config.can_config; }

mqtt_config_t *get_mqtt_config(void) { return &gateway_config.mqtt_config; }

tcp_config_t *get_tcp_config(void) { return &gateway_config.tcp_config; }

udp_config_t *get_udp_config(void) { return &gateway_config.udp_config; }
