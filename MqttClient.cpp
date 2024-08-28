/*!
 * @file 	    MqttClient.cpp
 * @brief 	    MqttClient
 * @author 	    Tom Christ
 * @date 	    2024-05-05
 * @copyright   Copyright (c) 2024 Tom Christ; MIT License   
 * @version	    0.1		Initial Version
 */

#include "MqttClient.h"

#define LOG_LOCAL_LEVEL ESP_LOG_WARN
#include "esp_log.h"
#define TAG "MQTTCLIENT"

using namespace std;

void mqtt_event_handler(void* handler_args, esp_event_base_t base,
    int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        if(!MqttClient::Singleton.isConnected()){
            //Client was not connected before, fire connected event
            MqttClient::Singleton.fireEvent(MqttClient::Event::CONNECTED);
        }
        MqttClient::Singleton.setConnected(true);
        ESP_LOGI(TAG, "event_handler: mqtt client connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        if(MqttClient::Singleton.isConnected()){
            //Client was connected before, fire disconnected event
            MqttClient::Singleton.fireEvent(MqttClient::Event::DISCONNECTED);
        }
        MqttClient::Singleton.setConnected(false);
        ESP_LOGI(TAG,
            "event_handler: mqtt client disconnected, reconnecting...");
        esp_mqtt_client_reconnect(MqttClient::Singleton.mqttClient);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "event_handler: subscribed to topic");
        if(xSemaphoreGive(MqttClient::Singleton.waitForSubscribed) != pdTRUE){
            ESP_LOGE(TAG, "event_handler: subscribed event: notification failed");
        }
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "event_handler: unsubscribed from topic");
        //TODO: unsubscirbing is not implemented yet
        break;
    case MQTT_EVENT_PUBLISHED:
        // THIS ISNT CALLED WHEN PUPLISHED, BUG?????
        ESP_LOGI(TAG, "event_handler: published msg");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "event_handler: received data");
        for(MqttClient::topicReceiver_t const& receiver: MqttClient::Singleton.topicReceivers){
            //TODO: find better solution for getting the topic...
            string topic = "";
            for(int i = 0; i < event->topic_len; i++){
                topic += (event->topic)[i];
            }
            if(receiver.topic == topic){
                int sendLength = event->data_len;
                if(sendLength >= receiver.maxStrLen){
                    ESP_LOGW(TAG,"event_handler: data event: received length does not fit in message buffer, cutting off payload");
                    sendLength = receiver.maxStrLen -1;
                }
                //Add null terminator to string
                event->data[sendLength] = 0;
                sendLength++;
                size_t sentBytes = xMessageBufferSend(*(receiver.messageBufferHandle), event->data, sendLength, 0);
                if(sentBytes == 0){
                    ESP_LOGE(TAG,"event_handler: data event: xMessageBufferSend error");
                }
            }
        }
        break;
    case MQTT_EVENT_ERROR:
        //TODO: commented because of error in logging function... dont know what is causing the error...
        //ESP_LOGE(TAG, "event_handler: error event: %s", event->data);
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGD(TAG, "event_handler: before connect event");
        //TODO: check if neccessary
        break;
    default:
        ESP_LOGW(TAG, "event_handler: received unknown event");
        break;
    }
}

MqttClient MqttClient::Singleton;

MqttClient& MqttClient::getInstance()
{
    return Singleton;
}

void MqttClient::setConnected(bool connected){
    xSemaphoreTake(Singleton.connectedMutex, portMAX_DELAY);
    Singleton.connected = connected;
    xSemaphoreGive(Singleton.connectedMutex);
}

MqttClient::MqttClient()
{
    connectedMutex = NULL;
    mqttClient = NULL;

    initialized = false;
    connected = false;

    keepAliveMS = 0;
    keepAliveTopic = "";
    keepAlivePayload = "";
    keepAliveTask = NULL;

    logBuffer = "";
    logTopic = "";
}

void MqttClient::init(Config const& config)
{
    const static string EXCEP_TAG = "MqttClient::init: ";

    esp_log_level_set(TAG,LOG_LOCAL_LEVEL);
    esp_log_level_set("mqtt_client",LOG_LOCAL_LEVEL);

    ESP_LOGD(TAG, "Init Started");

    if (initialized) {
        ESP_LOGW(TAG, "init: client is already initalized, doing nothing");
        return;
    }

    connectedMutex = xSemaphoreCreateMutex();

    waitForSubscribed = xSemaphoreCreateBinary();
    xSemaphoreGive(waitForSubscribed);

    this->keepAliveMS = config.keepAliveMS;
    this->keepAliveTopic = config.keepAliveTopic;
    this->keepAlivePayload = config.keepAlivePayload;

    memset(&mqttClientConf, 0, sizeof(esp_mqtt_client_config_t));

    mqttClientConf.broker.address.hostname = config.host.c_str();
    mqttClientConf.broker.address.port = 1883;
    mqttClientConf.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    mqttClientConf.credentials.username = config.username.c_str();
    mqttClientConf.credentials.authentication.password = config.password.c_str();
    mqttClientConf.credentials.client_id = config.clientID.c_str();
    mqttClientConf.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;

    if(config.lastWillTopic != "" && config.lastWillPayload != ""){
        //set last will.
         mqttClientConf.session.last_will.msg = config.lastWillPayload.c_str();
         mqttClientConf.session.last_will.topic = config.lastWillTopic.c_str();
    }

        mqttClient = esp_mqtt_client_init(&mqttClientConf);

    if (mqttClient == NULL) {
        throw runtime_error(EXCEP_TAG + "esp_mqtt_client_init() failed");
    }

    initialized = true;

    ESP_LOGI(TAG, "Init Done");
}

void MqttClient::connect()
{
    ESP_LOGD(TAG, "Connect started");
    const static string EXCEP_TAG = "MqttClient::connect: ";
    if (!initialized) {
        throw runtime_error(EXCEP_TAG + "client is not initialized");
    }

    if (isConnected()) {
        // client is already connected, doing nothing
        ESP_LOGD(TAG, "Connect called, but already connected, doing nothing");
        return;
    }

    esp_err_t result;

    result = esp_mqtt_client_register_event(mqttClient,
        (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler,
        mqttClient);
    if (result != ESP_OK) {
        throw runtime_error(EXCEP_TAG + "can't register event, error: " + esp_err_to_name(result));
    }

    result = esp_mqtt_client_start(mqttClient);
    if (result != ESP_OK) {
        throw runtime_error(EXCEP_TAG + "can't start client, error: " + esp_err_to_name(result));
    }

    if (keepAliveMS > 0) {
        // keepAlive should be active
        xTaskCreate(mqtt_client_keepAliveTaskCallback, "MQTT_KEEP_ALIVE_TSK",
            configMINIMAL_STACK_SIZE*1.5, NULL, mqttClientConf.task.priority + 1,
            &keepAliveTask);
    }
    ESP_LOGD(TAG, "Connect Done");
}

void MqttClient::disconnect()
{
    const static string EXCEP_TAG = "MqttClient::disconnect: ";
    if (!initialized) {
        // client is not initialized, doing nothing
        return;
    }
    if (!isConnected()) {
        // client is already disconnected, doing nothing.
        return;
    }

    esp_err_t result;

    result = esp_mqtt_client_stop(mqttClient);
    if (result != ESP_OK) {
        throw runtime_error(EXCEP_TAG + "can't stop client, error: " + esp_err_to_name(result));
    }

    if (keepAliveMS > 0) {
        vTaskDelete(keepAliveTask);
        keepAliveTask = NULL;
    }
}

bool MqttClient::isConnected()
{
    bool localConnected = false;
    xSemaphoreTake(connectedMutex, portMAX_DELAY);
    localConnected = connected;
    xSemaphoreGive(connectedMutex);
    return localConnected;
}

void MqttClient::publish(const std::string& topic,
    const std::string& payload) const
{
    const static string EXCEP_TAG = "MqttClient::publish: ";
    if (!initialized) {
        throw runtime_error(EXCEP_TAG + "client not initialized");
    }
    if (!Singleton.isConnected()) {
        throw runtime_error(EXCEP_TAG + "client not connected");
    }
    int result = -1;
    result = esp_mqtt_client_publish(mqttClient, topic.c_str(), payload.c_str(),
        0, 0, 0);
    if (result < 0) {
        throw runtime_error(EXCEP_TAG + "failed, error: " + esp_err_to_name(result));
    }
}

int mqtt_client_printf(const char* str, va_list list)
{
    MqttClient& context = MqttClient::Singleton;
    int retVal = EOF;

    // Send over old log output...
    if(MQTT_OLD_LOG_OUTPUT == NULL){
        //Error, if the old log output is not set, this function should not be entered...
        printf("%s: ERROR: mqtt_client_printf: Old Log output is not set.",TAG);
        return retVal;
    }else{
        retVal = MQTT_OLD_LOG_OUTPUT(str, list);
    }

    if (!context.isConnected()) {
        // No connection, clear buffer and return
        context.logBuffer.clear();
        return retVal; // For mqtt all is ok
    }

    // get formatted string
    static const unsigned int maxStrSize = 200;
    bool overflowOccur = false;
    char formatted[maxStrSize];
    memset(formatted, 0, maxStrSize);

    int ret = vsnprintf(formatted, maxStrSize, str, list);

    // Check for overflow
    if (ret < 0 || ret >= maxStrSize) {
        overflowOccur = true;
    }

    // Add all chars to buffer
    for (int i = 0; i < strlen(formatted); i++) {
        context.logBuffer += formatted[i];
    }

    // Handle Overflow
    if (overflowOccur) {
        // Send what we have...
        context.logBuffer[context.logBuffer.length() - 1] = '\n';
    }

    char lastChar = context.logBuffer[context.logBuffer.length() - 1];
    if (lastChar == '\n') {
        // end line is received, send it...

        // trim escape sequence at beginning
        if (context.logBuffer.at(0) == 0x1B) {
            // Buffer starts with control sequence, find the end
            for (int i = 0; i < context.logBuffer.length(); i++) {
                if (context.logBuffer.at(i) == 'm') {
                    // Control sequence terminator.
                    context.logBuffer = context.logBuffer.substr(i + 1);
                    break;
                }
            }
        }

        // trim escape sequence at the end
        for (int i = 0; i < context.logBuffer.length(); i++) {
            if (context.logBuffer.at(i) == 0x1B) {
                // Control sequence begin
                context.logBuffer = context.logBuffer.substr(0, i);
                break;
            }
        }

        // send
        try {
            if (overflowOccur) {
                context.publish(context.logTopic, "WARNING: Next log output is incomplete caused by buffer overflow:");
            }
            context.publish(context.logTopic, context.logBuffer);
        } catch (...) {
            printf("ERROR: MqttClient:putChar, failed to publish\n");
        }

        // delete buffer
        context.logBuffer = "";
    }
    return retVal;
}

void MqttClient::enableLogging(const std::string& topic)
{
    logTopic = topic;
    if(MQTT_OLD_LOG_OUTPUT == NULL){
        MQTT_OLD_LOG_OUTPUT = esp_log_set_vprintf(mqtt_client_printf);
    }else{
        esp_log_set_vprintf(mqtt_client_printf);
    }
}

void MqttClient::registerEventReceiver(QueueHandle_t& queueHandle, uint8_t queueSize){
    if(queueSize == 0){
        throw invalid_argument("MqttClient::registerEventReceiver: Queue size must be greater 0");
    }
    queueHandle = xQueueCreate(queueSize,sizeof(MqttClient::Event));
    if(queueHandle == 0){
        throw runtime_error("MqttClient::registerEventReceiver: Queue could not be created");
    }
    eventReceivers.push_back(&queueHandle);
}

void MqttClient::registerTopicReceiver(std::string const& topic, MessageBufferHandle_t& messageBufferHandle, size_t maxStrSize, TickType_t waitTime){
    if(maxStrSize == 0){
        throw invalid_argument("MqttClient::registerTopicReceiver: max string size must be greater 0");
    }

    if(!isConnected()){
        throw runtime_error("MqttClient::registerTopicReceiver: could not subscribe to topic, client not connected");
    }

    BaseType_t resultSema = xSemaphoreTake(waitForSubscribed,0);
    if(resultSema != pdTRUE){
        throw runtime_error("MqttClient::registerTopicReceiver: could not subscribe to topic, other task is currently subscribing");
    }

    //+4 because message buffer needs 4 bytes to store the msg length
    messageBufferHandle = xMessageBufferCreate(maxStrSize + 4);
    if(messageBufferHandle == 0){
        throw runtime_error("MqttClient::registerTopicReceiver: Message buffer could not be created");
    }

    topicReceiver_t receiver;
    receiver.topic = topic;
    receiver.messageBufferHandle = &messageBufferHandle;
    receiver.maxStrLen = maxStrSize;
    topicReceivers.push_back(receiver);

    esp_mqtt_client_subscribe(mqttClient,topic.c_str(),0);

    resultSema = xSemaphoreTake(waitForSubscribed,waitTime);
    xSemaphoreGive(waitForSubscribed);

    if(resultSema != pdTRUE){
        vMessageBufferDelete(messageBufferHandle);
        topicReceivers.pop_back();
        xSemaphoreGive(waitForSubscribed);
        throw runtime_error("MqttClient::registerTopicReceiver: could not subscribe to topic, timeout occured");
    }
}

void MqttClient::fireEvent(Event event){
    for(QueueHandle_t* queue: eventReceivers){
        BaseType_t result = xQueueSend(*queue,&event,0);
        if(result != pdTRUE){
            ESP_LOGE(TAG,"Could not fire event, receive queue is full.");
        }
    }
}

void mqtt_client_keepAliveTaskCallback(void* param)
{
    MqttClient& context = MqttClient::Singleton;
    while(1){
        if (context.isConnected()) {
            try {
                context.publish(context.keepAliveTopic,
                    context.keepAlivePayload);
            } catch (...) {
                ESP_LOGE(TAG, "keepAlive: can't publish keepAlive");
            }
        } else {
            ESP_LOGW(TAG, "keepAlive: client not connected");
        }
        vTaskDelay(pdMS_TO_TICKS(context.keepAliveMS));
    };
}