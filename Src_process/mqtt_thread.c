#include "thread_func.h"

// MQTT Configuration for ThingsBoard
#define MQTT_BROKER_HOST "demo.thingsboard.io"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "gateway_device"
#define MQTT_USERNAME "t7gsjo00dj3ca0ocyb1f"
#define MQTT_PASSWORD ""

// ThingsBoard Topics
#define MQTT_TOPIC_TELEMETRY "v1/devices/me/telemetry"
#define MQTT_TOPIC_ATTRIBUTES "v1/devices/me/attributes"
#define MQTT_QOS 1

// Function to get local IP address (unchanged)
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

// UPDATED: Raw data structures instead of parsed data
shared_data_t mqtt_data_n1 = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

shared_data_t mqtt_data_n2 = {
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// ========== RAW DATA Helper Functions ==========

// UPDATED: Store raw data for Node1 (12 bytes)
void set_mqtt_data_n1_raw(unsigned char *raw_bytes, int length) {
    pthread_mutex_lock(&mqtt_data_n1.mutex);
    
    // Free old data if exists
    if (mqtt_data_n1.data) {
        raw_data_t *old_data = (raw_data_t*)mqtt_data_n1.data;
        if (old_data->data) free(old_data->data);
        free(old_data);
    }
    
    // Store new raw data
    raw_data_t *new_data = malloc(sizeof(raw_data_t));
    if (new_data) {
        new_data->length = length;
        new_data->data = malloc(length);
        if (new_data->data) {
            memcpy(new_data->data, raw_bytes, length);
            mqtt_data_n1.data = new_data;
            pthread_cond_signal(&mqtt_data_n1.cond);
        } else {
            free(new_data);
        }
    }
    
    pthread_mutex_unlock(&mqtt_data_n1.mutex);
}

// UPDATED: Store raw data for Node2 (2 bytes)
void set_mqtt_data_n2_raw(unsigned char *raw_bytes, int length) {
    pthread_mutex_lock(&mqtt_data_n2.mutex);
    
    // Free old data if exists
    if (mqtt_data_n2.data) {
        raw_data_t *old_data = (raw_data_t*)mqtt_data_n2.data;
        if (old_data->data) free(old_data->data);
        free(old_data);
    }
    
    // Store new raw data
    raw_data_t *new_data = malloc(sizeof(raw_data_t));
    if (new_data) {
        new_data->length = length;
        new_data->data = malloc(length);
        if (new_data->data) {
            memcpy(new_data->data, raw_bytes, length);
            mqtt_data_n2.data = new_data;
            pthread_cond_signal(&mqtt_data_n2.cond);
        } else {
            free(new_data);
        }
    }
    
    pthread_mutex_unlock(&mqtt_data_n2.mutex);
}

// UPDATED: Get raw data for Node1
raw_data_t get_mqtt_data_n1_raw() {
    pthread_mutex_lock(&mqtt_data_n1.mutex);
    raw_data_t result = {NULL, 0};
    
    if (mqtt_data_n1.data) {
        raw_data_t *stored_data = (raw_data_t*)mqtt_data_n1.data;
        if (stored_data->data && stored_data->length > 0) {
            result.length = stored_data->length;
            result.data = malloc(result.length);
            if (result.data) {
                memcpy(result.data, stored_data->data, result.length);
            }
        }
    }
    
    pthread_mutex_unlock(&mqtt_data_n1.mutex);
    return result;
}

// UPDATED: Get raw data for Node2
raw_data_t get_mqtt_data_n2_raw() {
    pthread_mutex_lock(&mqtt_data_n2.mutex);
    raw_data_t result = {NULL, 0};
    
    if (mqtt_data_n2.data) {
        raw_data_t *stored_data = (raw_data_t*)mqtt_data_n2.data;
        if (stored_data->data && stored_data->length > 0) {
            result.length = stored_data->length;
            result.data = malloc(result.length);
            if (result.data) {
                memcpy(result.data, stored_data->data, result.length);
            }
        }
    }
    
    pthread_mutex_unlock(&mqtt_data_n2.mutex);
    return result;
}

// FIXED: Raw data to hex string conversion (fix from previous conversation)
void raw_data_to_hex_string(unsigned char *data, int length, char *hex_str, int hex_str_size) {
    if (hex_str_size < 1) return;
    
    int pos = 0;
    for (int i = 0; i < length && pos + 3 <= hex_str_size; i++) {
        sprintf(hex_str + pos, "%02X", data[i]);
        pos += 2;
    }
    hex_str[pos] = '\0';
}

int wait_for_mqtt_data_by_node(int node_type, int timeout_ms) {
    shared_data_t *mqtt_data = NULL;
    
    switch (node_type) {
        case NODE_TYPE_1:
            mqtt_data = &mqtt_data_n1;
            break;
        case NODE_TYPE_2:
            mqtt_data = &mqtt_data_n2;
            break;
        default:
            return 0;
    }
    
    pthread_mutex_lock(&mqtt_data->mutex);
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec += 1;
        timeout.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(&mqtt_data->cond, &mqtt_data->mutex, &timeout);
    pthread_mutex_unlock(&mqtt_data->mutex);
    return (result == 0) ? 1 : 0;
}

// Global MQTT variables
struct mosquitto *mqtt_client = NULL;
volatile int mqtt_connected = 0;

// MQTT Callbacks (unchanged)
void on_mqtt_connect(struct mosquitto *mosq, void *userdata, int result) {
    if (result == 0) {
        mqtt_connected = 1;
        
        // Send device attributes to ThingsBoard
        char attributes[256];
        snprintf(attributes, sizeof(attributes),
            "{"
            "\"gateway_ip\":\"%s\","
            "\"firmware_version\":\"1.0.0\","
            "\"device_type\":\"IoT Gateway\","
            "\"node_count\":2"
            "}", get_local_ip());
        
        mosquitto_publish(mqtt_client, NULL, MQTT_TOPIC_ATTRIBUTES,
                         strlen(attributes), attributes, MQTT_QOS, false);
    } else {
        mqtt_connected = 0;
    }
}

void on_mqtt_disconnect(struct mosquitto *mosq, void *userdata, int result) {
    mqtt_connected = 0;
}

void on_mqtt_publish(struct mosquitto *mosq, void *userdata, int mid) {
    // Message published successfully
}

// ==================== MQTT THREAD (RAW DATA MODE) ====================
void *mqtt_thread_func(void *arg) {
    mosquitto_lib_init();
    
    mqtt_client = mosquitto_new(MQTT_CLIENT_ID, true, NULL);
    if (!mqtt_client) {
        return NULL;
    }
    
    mosquitto_username_pw_set(mqtt_client, MQTT_USERNAME, MQTT_PASSWORD);
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);
    
    int rc = mosquitto_connect(mqtt_client, MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }
    
    mosquitto_loop_start(mqtt_client);
    
    // Wait for connection  
    int connection_timeout = 50;
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
    
    // Main MQTT loop - UPDATED for raw data
    time_t last_publish = 0;
    
    while (1) {
        time_t current_time = time(NULL);
        
        // Send raw telemetry data every second
        if (current_time - last_publish >= 1) {
            char telemetry_payload[2048];
            
            // Get raw data from both nodes
            raw_data_t node1_raw = get_mqtt_data_n1_raw();
            raw_data_t node2_raw = get_mqtt_data_n2_raw();
            
            // Convert raw data to hex strings
            char node1_hex[256] = "";
            char node2_hex[64] = "";
            
            if (node1_raw.data && node1_raw.length > 0) {
                raw_data_to_hex_string(node1_raw.data, node1_raw.length, node1_hex, sizeof(node1_hex));
            }
            
            if (node2_raw.data && node2_raw.length > 0) {
                raw_data_to_hex_string(node2_raw.data, node2_raw.length, node2_hex, sizeof(node2_hex));
            }
            
            // UPDATED: ThingsBoard raw data format
            snprintf(telemetry_payload, sizeof(telemetry_payload),
                "{"
                "\"timestamp\":%ld000,"
                "\"node1_raw_data\":\"%s\","
                "\"node1_data_length\":%d,"
                "\"node2_raw_data\":\"%s\","
                "\"node2_data_length\":%d,"
                "\"gateway_ip\":\"%s\","
                "\"data_source\":\"gateway_device\""
                "}", 
                current_time, 
                node1_hex, node1_raw.length,
                node2_hex, node2_raw.length,
                get_local_ip());

            rc = mosquitto_publish(mqtt_client, NULL, MQTT_TOPIC_TELEMETRY,
                                 strlen(telemetry_payload), telemetry_payload, MQTT_QOS, false);
            
            if (rc == MOSQ_ERR_SUCCESS) {
                last_publish = current_time;
            }
            
            // Clean up allocated memory
            if (node1_raw.data) free(node1_raw.data);
            if (node2_raw.data) free(node2_raw.data);
        }
        
        // Check connection and reconnect if needed
        if (!mqtt_connected) {
            mosquitto_reconnect(mqtt_client);
            usleep(1000 * 1000);
        }
        
        usleep(100 * 1000); // 100ms delay
    }
    
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mosquitto_lib_cleanup();
    
    return NULL;
}
