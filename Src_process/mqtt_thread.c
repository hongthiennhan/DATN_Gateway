#include <mosquitto.h>  // Thêm MQTT library header
#include "thread_func.h"
// MQTT Configuration
#define MQTT_BROKER_HOST "localhost"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "gateway_device"
#define MQTT_TOPIC_NODE1 "gateway/node1/status"
#define MQTT_TOPIC_NODE2 "gateway/node2/adc"
#define MQTT_QOS 1

shared_data_t mqtt_data_n1 = { // array of 3 int values 1, 2, 3 sended from Node1
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};
shared_data_t mqtt_data_n2 = { // adc value from Node2
    .data = NULL,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// ========== MQTT Data Helper Functions ==========

// Node1 MQTT data helpers (3 int values: T1, T2, T3)

void set_mqtt_data_n1(int t1, int t2, int t3) {
    pthread_mutex_lock(&mqtt_data_n1.mutex);
    if (mqtt_data_n1.data == NULL) {
        mqtt_data_n1.data = malloc(sizeof(node1_data_t));
    }
    node1_data_t *data = (node1_data_t*)mqtt_data_n1.data;
    data->t1 = t1;
    data->t2 = t2;
    data->t3 = t3;
    pthread_cond_signal(&mqtt_data_n1.cond);  // Notify MQTT thread
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

// Node2 MQTT data helpers (ADC value)
void set_mqtt_data_n2(uint16_t adc_value) {
    pthread_mutex_lock(&mqtt_data_n2.mutex);
    if (mqtt_data_n2.data == NULL) {
        mqtt_data_n2.data = malloc(sizeof(uint16_t));
    }
    *(uint16_t*)mqtt_data_n2.data = adc_value;
    pthread_cond_signal(&mqtt_data_n2.cond);  // Notify MQTT thread
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

// Wait for MQTT data updates
// Alternative: Wait for MQTT data by node type
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
            return 0; // Invalid node type
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
volatile int program_should_exit = 0;  // Exit flag for all threads

// MQTT Callbacks
void on_mqtt_connect(struct mosquitto *mosq, void *userdata, int result) {
    if (result == 0) {
        mqtt_connected = 1;
        printf("MQTT Connected to broker\n");
    } else {
        mqtt_connected = 0;
        printf("MQTT Connection failed: %s\n", mosquitto_strerror(result));
    }
}

void on_mqtt_disconnect(struct mosquitto *mosq, void *userdata, int result) {
    mqtt_connected = 0;
    printf("MQTT Disconnected from broker\n");
}

void on_mqtt_publish(struct mosquitto *mosq, void *userdata, int mid) {
    printf("MQTT Message published (mid: %d)\n", mid);
}

// ==================== MQTT THREAD ====================
void *mqtt_thread_func(void *arg) {
    printf("MQTT Thread started\n");
    
    // Initialize mosquitto library
    mosquitto_lib_init();
    
    // Create MQTT client
    mqtt_client = mosquitto_new(MQTT_CLIENT_ID, true, NULL);
    if (!mqtt_client) {
        printf("MQTT: Failed to create client\n");
        return NULL;
    }
    
    // Set callbacks
    mosquitto_connect_callback_set(mqtt_client, on_mqtt_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_mqtt_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_mqtt_publish);
    
    // Connect to broker
    printf("MQTT: Connecting to broker %s:%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    int rc = mosquitto_connect(mqtt_client, MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        printf("MQTT: Connect failed: %s\n", mosquitto_strerror(rc));
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }
    
    // Start network loop in non-blocking mode
    mosquitto_loop_start(mqtt_client);
    
    // Wait for connection
    int connection_timeout = 50; // 5 seconds
    while (!mqtt_connected && connection_timeout > 0 && !program_should_exit) {
        usleep(100 * 1000); // 100ms
        connection_timeout--;
    }
    
    if (!mqtt_connected) {
        printf("MQTT: Connection timeout\n");
        mosquitto_loop_stop(mqtt_client, true);
        mosquitto_destroy(mqtt_client);
        mosquitto_lib_cleanup();
        return NULL;
    }
    
    printf("MQTT Thread ready - waiting for data...\n");
    
    // Main MQTT loop
    time_t last_node1_publish = 0;
    time_t last_node2_publish = 0;
    
    while (!program_should_exit) {
        // Check for Node1 data updates
        if (wait_for_mqtt_data_by_node(NODE_TYPE_1, 500)) { // 500ms timeout
            time_t current_time = time(NULL);
            
            // Publish Node1 data (with rate limiting - max once per second)
            if (current_time - last_node1_publish >= 1) {
                node1_data_t data = get_mqtt_data_n1();
                
                char json_payload[256];
                snprintf(json_payload, sizeof(json_payload),
                    "{"
                    "\"timestamp\":%ld,"
                    "\"node_id\":1,"
                    "\"node_type\":\"motor_controller\","
                    "\"data\":{"
                        "\"t1\":%d,"
                        "\"t2\":%d,"
                        "\"t3\":%d"
                    "}"
                    "}", current_time, data.t1, data.t2, data.t3);
                
                rc = mosquitto_publish(mqtt_client, NULL, MQTT_TOPIC_NODE1, 
                                     strlen(json_payload), json_payload, MQTT_QOS, false);
                
                if (rc == MOSQ_ERR_SUCCESS) {
                    printf("MQTT: Published Node1 data - T1:%d, T2:%d, T3:%d\n", 
                           data.t1, data.t2, data.t3);
                    last_node1_publish = current_time;
                } else {
                    printf("MQTT: Publish failed for Node1: %s\n", mosquitto_strerror(rc));
                }
            }
        }
        
        // Check for Node2 data updates
        if (wait_for_mqtt_data_by_node(NODE_TYPE_2, 500)) { // 500ms timeout
            time_t current_time = time(NULL);
            
            // Publish Node2 data (with rate limiting - max once per second)
            if (current_time - last_node2_publish >= 1) {
                uint16_t adc_value = get_mqtt_data_n2();
                float voltage = (adc_value / 4095.0) * 3.3; // Convert to voltage
                
                char json_payload[256];
                snprintf(json_payload, sizeof(json_payload),
                    "{"
                    "\"timestamp\":%ld,"
                    "\"node_id\":2,"
                    "\"node_type\":\"sensor_board\","
                    "\"data\":{"
                        "\"adc_raw\":%d,"
                        "\"voltage\":%.3f"
                    "}"
                    "}", current_time, adc_value, voltage);
                
                rc = mosquitto_publish(mqtt_client, NULL, MQTT_TOPIC_NODE2, 
                                     strlen(json_payload), json_payload, MQTT_QOS, false);
                
                if (rc == MOSQ_ERR_SUCCESS) {
                    printf("MQTT: Published Node2 data - ADC:%d (%.3fV)\n", 
                           adc_value, voltage);
                    last_node2_publish = current_time;
                } else {
                    printf("MQTT: Publish failed for Node2: %s\n", mosquitto_strerror(rc));
                }
            }
        }
        
        // Reconnect if connection lost
        if (!mqtt_connected) {
            printf("MQTT: Attempting to reconnect...\n");
            mosquitto_reconnect(mqtt_client);
            usleep(1000 * 1000); // Wait 1 second before retry
        }
    }
    
    // Cleanup
    printf("MQTT Thread shutting down...\n");
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mosquitto_lib_cleanup();
    
    printf("MQTT Thread exited\n");
    return NULL;
}
