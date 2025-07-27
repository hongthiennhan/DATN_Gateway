#include "thread_func.h"

// MQTT Configuration for ThingsBoard
#define MQTT_BROKER_HOST "demo.thingsboard.io"  // ThingsBoard server
// #define MQTT_BROKER_HOST "your-thingsboard-server.com"  // Hoặc server riêng
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "gateway_device"
#define MQTT_USERNAME "iko2iokzzhdd5do5zqk3"  // ThingsBoard access token
#define MQTT_PASSWORD ""  // Để trống cho ThingsBoard

// ThingsBoard Topics
#define MQTT_TOPIC_TELEMETRY "v1/devices/me/telemetry"
#define MQTT_TOPIC_ATTRIBUTES "v1/devices/me/attributes"
#define MQTT_QOS 1

// Function to get local IP address
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

// MQTT Callbacks
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

// ==================== MQTT THREAD ====================
void *mqtt_thread_func(void *arg) {
    mosquitto_lib_init();

    mqtt_client = mosquitto_new(MQTT_CLIENT_ID, true, NULL);
    if (!mqtt_client) {
        return NULL;
    }
    
    // Set username (access token) for ThingsBoard authentication
    mosquitto_username_pw_set(mqtt_client, MQTT_USERNAME, MQTT_PASSWORD);
    
    // Set callbacks
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);
    
    // Connect to ThingsBoard server
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
    
    // Main MQTT loop
    time_t last_publish = 0;
    
    while (1) {
        // Send command to uart thread to get data
        if (!is_busy) {
            if( shared_node_type == NODE_TYPE_1) {
                set_command_code(6); // Command to get Node1 data
            } 
            else if (shared_node_type == NODE_TYPE_2) {
                set_command_code(3); // Command to get Node2 ADC value
            }
            command_pending = 1;
        }

        time_t current_time = time(NULL);
        
        // Combine and send telemetry data every second
        if (current_time - last_publish >= 1) {
            char telemetry_payload[1024];
            
            // Get data from both nodes
            node1_data_t node1_data = get_mqtt_data_n1();
            uint16_t node2_adc = get_mqtt_data_n2();
            
            // ThingsBoard telemetry format
            snprintf(telemetry_payload, sizeof(telemetry_payload),
                "{"
                "\"timestamp\":%ld000,"  // ThingsBoard expects milliseconds
                "\"node1_t1\":%d,"
                "\"node1_t2\":%d,"
                "\"node1_t3\":%d,"
                "\"node2_adc\":%d,"
                "\"gateway_ip\":\"%s\","
                "\"data_source\":\"gateway_device\""
                "}", current_time, node1_data.t1, node1_data.t2, node1_data.t3,
                node2_adc, get_local_ip());

            rc = mosquitto_publish(mqtt_client, NULL, MQTT_TOPIC_TELEMETRY,
                                 strlen(telemetry_payload), telemetry_payload, MQTT_QOS, false);
            
            if (rc == MOSQ_ERR_SUCCESS) {
                last_publish = current_time;
            }
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
