#include "thread_func.h"
#include "node_config.h"

// Get local IP address, excluding loopback and link-local addresses
char* get_local_ip() {
    static char ip_str[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddrs_ptr, *ifa;
    
    if (getifaddrs(&ifaddrs_ptr) == -1) {
        strcpy(ip_str, "127.0.0.1");
        return ip_str;
    }
    
    for (ifa = ifaddrs_ptr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
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

// Global MQTT variables
struct mosquitto *mqtt_client = NULL;
volatile int mqtt_connected = 0;


// Wait for MQTT data from specified node with timeout
int wait_for_mqtt_data_by_node(int node_type, int timeout_ms) {
    node_config_t *node = get_node_by_id(node_type);
    if (!node || !node->mqtt_data) return 0;
    
    pthread_mutex_lock(&node->mqtt_data->mutex);
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(&node->mqtt_data->cond, &node->mqtt_data->mutex, &timeout);
    pthread_mutex_unlock(&node->mqtt_data->mutex);
    return (result == 0) ? 1 : 0;
}

// Convert binary data to uppercase hex string (e.g., 0xAB -> "AB")
void raw_data_to_hex_string(unsigned char *data, int length, char *hex_str, int hex_str_size) {
    if (hex_str_size < 1) return;
    int pos = 0;
    for (int i = 0; i < length && pos + 3 <= hex_str_size; i++) {
        sprintf(hex_str + pos, "%02X", data[i]);
        pos += 2;
    }
    hex_str[pos] = '\0';
}

// MQTT connection established callback - publishes device attributes
void on_mqtt_connect(struct mosquitto *mosq, void *userdata, int result) {
    if (result == 0) {
        mqtt_connected = 1;
        
        mqtt_config_t *config = get_mqtt_config();
        system_info_t *sys_info = get_system_info();
        
        if (!config || !sys_info) return;
        
        char *attributes = malloc(config->attributes_buffer_size);
        if (!attributes) return;
        
        snprintf(attributes, config->attributes_buffer_size,
                "{"
                "\"gateway_ip\":\"%s\","
                "\"firmware_version\":\"%s\","
                "\"device_type\":\"%s\","
                "\"manufacturer\":\"%s\","
                "\"model\":\"%s\","
                "\"node_count\":%d"
                "}", 
                get_local_ip(), 
                sys_info->firmware_version,
                sys_info->device_type, 
                sys_info->manufacturer,
                sys_info->model,
                get_node_count());

        mosquitto_publish(mqtt_client, NULL, config->topic_attributes,
                          strlen(attributes), attributes, config->qos, false);
        
        free(attributes);
    } else {
        mqtt_connected = 0;
    }
}

// MQTT connection lost callback - sets disconnected flag
void on_mqtt_disconnect(struct mosquitto *mosq, void *userdata, int result) {
    mqtt_connected = 0;
}

// MQTT message publish confirmation callback
void on_mqtt_publish(struct mosquitto *mosq, void *userdata, int mid) {
    // Message published successfully
}

// Build JSON telemetry payload with raw data from all nodes
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp) {
    mqtt_config_t *config = get_mqtt_config();
    if (!config) return;
    
    char *temp_buffer = malloc(1024);
    if (!temp_buffer) return;
    
    // Start JSON with timestamp if configured
    if (config->system_fields.include_timestamp) {
        snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);
    } else {
        snprintf(payload, payload_size, "{");
    }

    // Add RAW data from all configured nodes
    for (int i = 0; i < get_node_count(); i++) {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->mqtt_data) continue;

        pthread_mutex_lock(&node->mqtt_data->mutex);
        if (node->mqtt_data->data) {
            raw_data_t *raw_data = (raw_data_t*)node->mqtt_data->data;
            
            if (raw_data && raw_data->data && raw_data->length > 0) {
                // Convert raw data to hex string
                char *hex_str = malloc(raw_data->length * 2 + 1);
                if (hex_str) {
                    raw_data_to_hex_string(raw_data->data, raw_data->length, hex_str, raw_data->length * 2 + 1);
                    
                    // Send raw data as hex string
                    snprintf(temp_buffer, 1024, ",\"node%d_raw_data\":\"%s\",\"node%d_data_length\":%d", 
                             node->node_id, hex_str, node->node_id, raw_data->length);
                    
                    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
                    free(hex_str);
                }
            }
        }
        pthread_mutex_unlock(&node->mqtt_data->mutex);
    }

    // Add system info based on config
    if (config->system_fields.include_gateway_ip) {
        snprintf(temp_buffer, 1024, ",\"gateway_ip\":\"%s\"", get_local_ip());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    
    if (config->system_fields.data_source[0] != '\0') {
        snprintf(temp_buffer, 1024, ",\"data_source\":\"%s\"", config->system_fields.data_source);
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    
    if (config->system_fields.include_node_count) {
        snprintf(temp_buffer, 1024, ",\"node_count\":%d", get_node_count());
        strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
    }
    
    strncat(payload, "}", payload_size - strlen(payload) - 1);
    free(temp_buffer);
}

// Main MQTT thread - handles connection, publishing telemetry data
void *mqtt_thread_func(void *arg) {
    mqtt_config_t *config = get_mqtt_config();
    if (!config) return NULL;
    
    mosquitto_lib_init();
    mqtt_client = mosquitto_new(config->client_id, true, NULL);
    if (!mqtt_client) return NULL;

    mosquitto_username_pw_set(mqtt_client, config->username, config->password);
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);

    int rc = mosquitto_connect(mqtt_client, config->broker_host, config->broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

    mosquitto_loop_start(mqtt_client);

    int connection_timeout = config->connection_timeout;
    while (!mqtt_connected && connection_timeout > 0) {
        usleep(100 * 1000);
        connection_timeout--;
    }

    if (!mqtt_connected) {
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }

    time_t last_publish = 0;
    char *telemetry_payload = malloc(config->payload_buffer_size);
    if (!telemetry_payload) {
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }
    
    while (1) {
        time_t current_time = time(NULL);
        
        if (current_time - last_publish >= config->publish_interval) {
            build_telemetry_payload(telemetry_payload, config->payload_buffer_size, current_time);

            rc = mosquitto_publish(mqtt_client, NULL, config->topic_telemetry,
                                   strlen(telemetry_payload), telemetry_payload, 
                                   config->qos, false);
            if (rc == MOSQ_ERR_SUCCESS) {
                last_publish = current_time;
            }
        }

        if (!mqtt_connected) {
            mosquitto_reconnect(mqtt_client);
            usleep(config->reconnect_delay_ms * 1000);
        }

        usleep(config->loop_interval_ms * 1000);
    }

    free(telemetry_payload);
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mosquitto_lib_cleanup();
    return NULL;
}
