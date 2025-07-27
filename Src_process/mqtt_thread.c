#include "thread_func.h"
#include "node_config.h"

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

// Shared MQTT data structures (unchanged)
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

// ========== MQTT Data Helper Functions ==========
void set_mqtt_data_n1(int t1, int t2, int t3) {
    pthread_mutex_lock(&mqtt_data_n1.mutex);
    if (mqtt_data_n1.data == NULL) {
        mqtt_data_n1.data = malloc(sizeof(node1_data_t));
    }
    node1_data_t *data = (node1_data_t*)mqtt_data_n1.data;
    data->t1 = t1;
    data->t2 = t2;
    data->t3 = t3;
    pthread_cond_signal(&mqtt_data_n1.cond);
    pthread_mutex_unlock(&mqtt_data_n1.mutex);
}

node1_data_t get_mqtt_data_n1() {
    pthread_mutex_lock(&mqtt_data_n1.mutex);
    node1_data_t result = {0, 0, 0};
    if (mqtt_data_n1.data != NULL) {
        result = *(node1_data_t*)mqtt_data_n1.data;
    }
    pthread_mutex_unlock(&mqtt_data_n1.mutex);
    return result;
}

void set_mqtt_data_n2(uint16_t adc_value) {
    pthread_mutex_lock(&mqtt_data_n2.mutex);
    if (mqtt_data_n2.data == NULL) {
        mqtt_data_n2.data = malloc(sizeof(uint16_t));
    }
    *(uint16_t*)mqtt_data_n2.data = adc_value;
    pthread_cond_signal(&mqtt_data_n2.cond);
    pthread_mutex_unlock(&mqtt_data_n2.mutex);
}

uint16_t get_mqtt_data_n2() {
    pthread_mutex_lock(&mqtt_data_n2.mutex);
    uint16_t result = 0;
    if (mqtt_data_n2.data != NULL) {
        result = *(uint16_t*)mqtt_data_n2.data;
    }
    pthread_mutex_unlock(&mqtt_data_n2.mutex);
    return result;
}

int wait_for_mqtt_data_by_node(int node_type, int timeout_ms) {
    shared_data_t *mqtt_data = NULL;
    
    // Use node_id instead of hard-coded NODE_TYPE constants
    if (node_type == 1) {  // Node1
        mqtt_data = &mqtt_data_n1;
    } else if (node_type == 2) {  // Node2
        mqtt_data = &mqtt_data_n2;
    } else {
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

// MQTT Callbacks using config
void on_mqtt_connect(struct mosquitto *mosq, void *userdata, int result) {
    if (result == 0) {
        mqtt_connected = 1;
        
        mqtt_config_t *config = get_mqtt_config();
        
        // Send device attributes to ThingsBoard
        char attributes[512];
        snprintf(attributes, sizeof(attributes),
                "{"
                "\"gateway_ip\":\"%s\","
                "\"firmware_version\":\"1.0.0\","
                "\"device_type\":\"IoT Gateway\","
                "\"node_count\":%d"
                "}", get_local_ip(), get_node_count());

        mosquitto_publish(mqtt_client, NULL, config->topic_attributes,
                          strlen(attributes), attributes, config->qos, false);
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

// Helper function to build telemetry payload dynamically
void build_telemetry_payload(char *payload, size_t payload_size, time_t timestamp) {
    char temp_buffer[512];
    
    // Start JSON
    snprintf(payload, payload_size, "{\"timestamp\":%ld000", timestamp);

    // Add data from all configured nodes
    for (int i = 0; i < get_node_count(); i++) {
        node_config_t *node = get_node_by_index(i);
        if (!node || !node->mqtt_data) continue;

        pthread_mutex_lock(&node->mqtt_data->mutex);
        if (node->mqtt_data->data) {
            if (strcmp(node->data_structure, "node1_data_t") == 0) {
                node1_data_t *data = (node1_data_t*)node->mqtt_data->data;
                snprintf(temp_buffer, sizeof(temp_buffer),
                         ",\"node%d_t1\":%d,\"node%d_t2\":%d,\"node%d_t3\":%d",
                         node->node_id, data->t1, node->node_id, data->t2, node->node_id, data->t3);
                strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
            }
            else if (strcmp(node->data_structure, "node2_data_t") == 0) {
                uint16_t *adc_data = (uint16_t*)node->mqtt_data->data;
                snprintf(temp_buffer, sizeof(temp_buffer),
                         ",\"node%d_adc\":%d", node->node_id, *adc_data);
                strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
            }
            // Additional node types can be easily added here by extending JSON config
        }
        pthread_mutex_unlock(&node->mqtt_data->mutex);
    }

    // Add system info
    snprintf(temp_buffer, sizeof(temp_buffer),
             ",\"gateway_ip\":\"%s\",\"data_source\":\"gateway_device\",\"node_count\":%d}",
             get_local_ip(), get_node_count());
    strncat(payload, temp_buffer, payload_size - strlen(payload) - 1);
}

// ==================== MQTT THREAD (Fully Config-driven) ====================
void *mqtt_thread_func(void *arg) {
    // Get MQTT configuration from JSON config
    mqtt_config_t *config = get_mqtt_config();
    
    if (!config) {
        printf("Error: MQTT configuration not found\n");
        return NULL;
    }
    
    printf("MQTT Config: Host=%s, Port=%d, Client=%s, Username=%s\n", 
           config->broker_host, config->broker_port, config->client_id, config->username);
    
    mosquitto_lib_init();
    mqtt_client = mosquitto_new(config->client_id, true, NULL);
    if (!mqtt_client) {
        printf("Error: Failed to create MQTT client\n");
        return NULL;
    }

    // Set username and password from config
    mosquitto_username_pw_set(mqtt_client, config->username, config->password);
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);

    // Connect using config values
    int rc = mosquitto_connect(mqtt_client, config->broker_host, config->broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("Error: Failed to connect to MQTT broker %s:%d (error: %d)\n", 
               config->broker_host, config->broker_port, rc);
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
        printf("Error: Failed to establish MQTT connection within timeout\n");
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }
    
    printf("MQTT connected successfully to %s:%d\n", config->broker_host, config->broker_port);

    // Main MQTT loop using config publish interval
    time_t last_publish = 0;
    while (1) {
        time_t current_time = time(NULL);
        
        // Send telemetry data based on config publish_interval
        if (current_time - last_publish >= config->publish_interval) {
            char telemetry_payload[2048];
            build_telemetry_payload(telemetry_payload, sizeof(telemetry_payload), current_time);

            // Publish to topic from config
            rc = mosquitto_publish(mqtt_client, NULL, config->topic_telemetry,
                                   strlen(telemetry_payload), telemetry_payload, 
                                   config->qos, false);
            if (rc == MOSQ_ERR_SUCCESS) {
                last_publish = current_time;
                printf("MQTT telemetry published: %s\n", telemetry_payload);
            } else {
                printf("Error: Failed to publish MQTT message (error: %d)\n", rc);
            }
        }

        // Check connection and reconnect if needed
        if (!mqtt_connected) {
            printf("MQTT connection lost, attempting to reconnect...\n");
            mosquitto_reconnect(mqtt_client);
            usleep(1000 * 1000);  // Wait 1 second before retry
        }

        usleep(100 * 1000);  // 100ms loop interval
    }

    // Cleanup
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mosquitto_lib_cleanup();
    printf("MQTT thread terminated\n");
    return NULL;
}
