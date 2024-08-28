/*!
 * @file 	    MqttClient.h
 * @brief 	    MqttClient
 * @author 	    Tom Christ
 * @date 	    2024-05-05
 * @copyright   Copyright (c) 2024 Tom Christ; MIT License   
 * @version	    0.1		Initial Version
 */

#ifndef MqttClient_H_
#define MqttClient_H_

#include <stdexcept>
#include <string>
#include <vector>

#include "esp_err.h"
#include "mqtt_client.h"

typedef int (*vprintf_like_t)(const char *, va_list);

static volatile vprintf_like_t MQTT_OLD_LOG_OUTPUT = NULL;

extern "C" void mqtt_event_handler(void* handler_args, esp_event_base_t base,
    int32_t event_id, void* event_data);

extern "C" int mqtt_client_printf(const char * str, va_list list);

extern "C" void mqtt_client_keepAliveTaskCallback(void* param);


class MqttClient {

friend void mqtt_event_handler(void* handler_args, esp_event_base_t base,
    int32_t event_id, void* event_data);

friend int mqtt_client_printf(const char * str, va_list list);

friend void mqtt_client_keepAliveTaskCallback(void* param);

/** ******************/
/** PUBLIC TYPEDEFS **/
/** ******************/
public:
    struct Config{
        std::string host = "";
        std::string username = "";
        std::string password = "";
        std::string clientID = ""; 
        unsigned int keepAliveMS = 0;
        std::string keepAliveTopic = "";
        std::string keepAlivePayload = "";
        std::string lastWillTopic = "";
        std::string lastWillPayload = "";
    };

    enum class Event{
        CONNECTED,
        DISCONNECTED
    };

/** *******************/
/** PRIVATE TYPEDEFS **/
/** *******************/
private:
    struct topicReceiver_t{
        std::string topic;
        MessageBufferHandle_t* messageBufferHandle;
        uint32_t maxStrLen;
    };

/** ****************************/
/** PRIVATE STATIC ATTRIBUTES **/
/** ****************************/
private:
    static MqttClient Singleton;

/** ************************/
/** PUBLIC STATIC METHODS **/
/** ************************/
public:
    static MqttClient& getInstance();

/** *************************/
/** PRIVATE STATIC METHODS **/
/** *************************/
static void setConnected(bool connected);

/** **************/
/** CONSTRUCTOR **/
/** **************/
private:
    MqttClient();

/** *************/
/** ATTRIBUTES **/
/** *************/    
private:
    SemaphoreHandle_t connectedMutex;
    esp_mqtt_client_handle_t mqttClient;
    esp_mqtt_client_config_t mqttClientConf;

    bool initialized;
    bool connected;

    unsigned int keepAliveMS;
    std::string keepAliveTopic;
    std::string keepAlivePayload;
    TaskHandle_t keepAliveTask;

    std::string logBuffer;
    std::string logTopic;

    std::vector<QueueHandle_t*> eventReceivers;

    std::vector<topicReceiver_t> topicReceivers;

    SemaphoreHandle_t waitForSubscribed;

/** *****************/
/** PUBLIC METHODS **/
/** *****************/
public:
    void init(Config const& config);
    void connect();
    void disconnect();
    bool isConnected();
    void publish(std::string const& topic, std::string const& payload) const;
    void enableLogging(std::string const& topic);
    void registerEventReceiver(QueueHandle_t& queueHandle, uint8_t queueSize = 1);
    void registerTopicReceiver(std::string const& topic, MessageBufferHandle_t& messageBufferHandle, size_t maxStrSize = 100, TickType_t waitTime = 1000);

/** ******************/
/** PRIVATE METHODS **/
/** ******************/
private:
    void fireEvent(Event event);
};

#endif /* MqttClient_H_ */