# ESP32_MqttClient_Component
**ESP-IDF component as a cpp interface class for FreeRTOS**

# Info
- IDE: VS Code with ESP-IDF Extension
- ESP-IDF version: 5.2.1

# Usage
- Must be placed within the ESP-IDF projects "components" folder

# Example
```c++
void run()
{
    //WIFI Config
    //...

    //MQTT Config
    MqttClient::Config myConfig;
    myConfig.host = "HOST IP";
    myConfig.username = "MQTT USER";
    myConfig.password = "MQTT PASSWORD";
    myConfig.clientID = "MQTT CLIENT ID";
    myConfig.keepAliveMS = 600000;
    myConfig.keepAliveTopic = "KEEP LIVE TOPIC";
    myConfig.keepAlivePayload = "KEEP LIVE PAYLOAD";
    myConfig.lastWillTopic = "LAST WILL TOPIC";
    myConfig.lastWillPayload = "LAST WILL PAYLOAD";

    MqttClient& mqttClient = MqttClient::getInstance();

    QueueHandle_t rcvMqttEventQueue;

    try{
        //WIFI Setup and Connect
        //...

        //MQTT Setup and Connect
        mqttClient.registerEventReceiver(rcvMqttEventQueue);
        mqttClient.init(CONFIG_MQTT);
        mqttClient.connect();

        MqttClient::Event mqttEvent;

        //Wait for connected
        bool mqttConnected = false;
        while(!mqttConnected){
            BaseType_t result = xQueueReceive(rcvMqttEventQueue,&mqttEvent,portMAX_DELAY);
            if(result == pdTRUE){
                if(mqttEvent == MqttClient::Event::CONNECTED){
                    ESP_LOGI(TAG, "Mqtt connected");
                    mqttConnected = true;
                }
                if(mqttEvent == MqttClient::Event::DISCONNECTED){
                    ESP_LOGI(TAG, "Mqtt disconnected");
                }
            }
        }

        //Publish something
        mqttClient.publish("stat/test","I'm connected.");

        //Receive from topic
        MessageBufferHandle_t mqttMsgBuffer;
        mqttClient.registerTopicReceiver("cmnd/test",mqttMsgBuffer);

        char receiveData[100];

        while(true){
            memset(receiveData,0,100);
            size_t rcvBytes = xMessageBufferReceive(mqttMsgBuffer,receiveData,100,portMAX_DELAY);
            if(rcvBytes != 0){
                ESP_LOGI(TAG,"Received msg: %s",receiveData);
            }
            vTaskDelay(10);
        }

    }catch(...){
        //Error Handling
    }
}
```