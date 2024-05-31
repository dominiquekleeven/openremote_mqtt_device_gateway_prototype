#include "config/secrets.h"
#include "external/OpenRemotePubSubClient/openremote_pubsub.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "models/device_message.h"
#include "modules/device_manager.h"
#include "models/asset_templates.h"

using namespace std;

#define REVISION 3

// Global Variables
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient); // passed to OpenRemotePubSub - which wraps PubSubClient
OpenRemotePubSub openRemotePubSub(mqtt_client_id, mqttClient);
Preferences preferences;
WiFiUDP udp;
DeviceManager deviceManager(preferences);

SemaphoreHandle_t xMutex;

// Function Prototypes
void mqttConnectionHandler(void *pvParameters);
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length);
void subscribeToActuators();
void udpHandler(void *pvParameters);
void udpHandleDataMessage(DeviceMessage deviceMessage);
void udpHandleOnboardMessage(DeviceMessage deviceMessage);

void setup()
{
  Serial.begin(115200);

  delay(5000); // delay for 5 seconds

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println("Connecting to WiFi, ssid: " + String(ssid) + +"...");
  }
  Serial.println("+ Connected to WiFi");
  wifiClient.setCACert(root_ca);

  // local address
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Preferences
  preferences.begin("device-manager" + REVISION, false);

  // MQTT
  openRemotePubSub.client.setServer(mqtt_host, mqtt_port);
  openRemotePubSub.client.setBufferSize(8192);
  openRemotePubSub.client.setCallback(mqttCallbackHandler);

  // Mutex
  xMutex = xSemaphoreCreateMutex();

  // Device Manager
  deviceManager.init();

  // Tasks - ADJUST HEAP SIZE BASED ON AVAILABLE MEMORY
  xTaskCreate(mqttConnectionHandler, "MQTT Connection Task", 20480, NULL, 1, NULL);
  xTaskCreate(udpHandler, "UDP Handler Task", 20480, NULL, 1, NULL);
}

// Core Loop
void loop()
{
  delay(10);
  openRemotePubSub.client.loop();
}

// MQTT Task
void mqttConnectionHandler(void *pvParameters)
{
  while (true)
  {
    // grab the mutex
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      if (!openRemotePubSub.client.connected())
      {
        Serial.println("Connecting to MQTT, host: " + String(mqtt_host) + ", port: " + String(mqtt_port) + ", client id: " + String(mqtt_client_id));
        if (openRemotePubSub.client.connect(mqtt_client_id, mqtt_user, mqtt_pas))
        {
          Serial.println("+ MQTT Connected");
          openRemotePubSub.updateAttribute("master", gatewayAssetId, "gatewayStatus", "3", false);
          subscribeToActuators();
        }
        else
        {
          Serial.println("MQTT Connection Failed");
        }
      }
      // release the mutex
      xSemaphoreGive(xMutex);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
// Callback function for MQTT
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received, topic: ");
  Serial.println(topic);

  if (strstr(topic, "response") != NULL)
  {
    Serial.println("+ Response Received");
    // unsubscribe from response topics, part of the request-response pattern
    openRemotePubSub.client.unsubscribe(topic);
    // handle response, parse payload to JSON
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload, length);
    bool isAssetEvent = doc["eventType"].as<String>() == "asset";
    bool isCreationEvent = doc["cause"].as<String>() == "CREATE";
    String asset = doc["asset"].as<String>();

    if (isAssetEvent && isCreationEvent)
    {
      Serial.println("+ Asset Created: " + asset);
      DeviceAsset deviceAsset = DeviceAsset::fromJson(asset);
      deviceManager.addDeviceAsset(deviceAsset);
    }
  }

  // Handle event subscriptions
  if (strstr(topic, "events") != NULL)
  {
  }
}

void subscribeToActuators()
{
  // Plug Actuators
  for (int i = 0; i < deviceManager.assets.size(); i++)
  {
    DeviceAsset asset = deviceManager.assets[i];
    if (asset.type == PLUG_ASSET)
    {
      Serial.println("Subscribing to actuator asset: " + asset.id);
      openRemotePubSub.subscribeToAssetAttribute("master", asset.id, "onOff");
    }
  }
}

void udpHandler(void *pvParameters)
{
  udp.begin(udp_port);
  while (true)
  {
    // grab mutex
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      if (openRemotePubSub.client.connected())
      {
        int packetSize = udp.parsePacket();
        if (packetSize)
        {
          char incomingPacket[255];
          udp.read(incomingPacket, 255);
          incomingPacket[packetSize] = 0;

          // Parse incoming packet to DeviceMessage
          DeviceMessage deviceMessage = DeviceMessage::fromJson(incomingPacket);

          if (deviceMessage.message_type == DATA_MESSAGE)
          {
            udpHandleDataMessage(deviceMessage);
          }

          // ONBOARDING
          if (deviceMessage.message_type == ONBOARD_MESSAGE)
          {
            udpHandleOnboardMessage(deviceMessage);
          }
        }
      }
      // release mutex
      xSemaphoreGive(xMutex);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void udpHandleDataMessage(DeviceMessage deviceMessage)
{
  Serial.println("Data received, device: " + deviceMessage.device_name + " sn: " + deviceMessage.device_sn + " data: " + deviceMessage.data);

  // Ensure device is onboarded before sending data
  if (!deviceManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    // send ONBOARDREQ (request device to go through onboarding process)
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_REQ, 11);
    udp.endPacket();
  }
  else
  {
    // Presence data of presence sensor devices
    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      String assetId = deviceManager.getDeviceAssetId(deviceMessage.device_sn.c_str());
      openRemotePubSub.updateAttribute("master", assetId, "presence", deviceMessage.data, false);
    }
  }
}

void udpHandleOnboardMessage(DeviceMessage deviceMessage)
{
  Serial.println("Onboard Message Received, device: " + deviceMessage.device_name + " sn: " + deviceMessage.device_sn);

  // Check if device is already onboarded on OpenRemote, prevent double onboarding
  if (deviceManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    Serial.println("+ Device is onboarded");

    DeviceAssetConnection connection;
    connection.ip = udp.remoteIP();
    connection.port = udp.remotePort();
    deviceManager.updateOrAddConnection(deviceMessage.device_sn.c_str(), connection);
    // Send ONBOARDOK back to udp
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_OK, 10);
    udp.endPacket();
  }
  // Check if device is pending onboarding, prevent double onboarding
  else if (deviceManager.isOnboardingPending(deviceMessage.device_sn.c_str()))
  {
    Serial.println("<> Device is pending onboarding");
  }
  // Start onboarding the device
  else
  {
    deviceManager.addPendingOnboarding(deviceMessage.device_sn.c_str());
    // Onboard the device with OpenRemote - based on device type
    if (deviceMessage.device_type == PLUG_ASSET)
    {
      PlugAsset asset = PlugAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      String json = asset.toJson();
      if (openRemotePubSub.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
      {
        Serial.println("+ Sent asset create request");
      }
    }

    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      PresenceSensorAsset asset = PresenceSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      String json = asset.toJson();
      if (openRemotePubSub.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
      {
        Serial.println("+ Sent asset create request");
      }
    }
  }
}
