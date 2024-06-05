#include "config/secrets.h"
#include "external/OpenRemotePubSubClient/openremote_pubsub.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "models/device_message.h"
#include "modules/device_asset_manager.h"
#include "models/asset_templates.h"

using namespace std;

// Simple versioning - used for resetting preferences
#define REVISION 5

// Global Variables
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient); // passed to OpenRemotePubSub - which wraps PubSubClient
OpenRemotePubSub openRemotePubSub(mqtt_client_id, mqttClient);
Preferences preferences;
WiFiUDP udp;
DeviceAssetManager deviceAssetManager(preferences);

SemaphoreHandle_t mqttClientMutex;

// Function Prototypes
void mqttConnectionHandler(void *pvParameters);
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length);
void udpHandler(void *pvParameters);
void udpHandleDataMessage(DeviceMessage deviceMessage);
void udpHandleOnboardMessage(DeviceMessage deviceMessage);
void udpHandleAliveMessage(DeviceMessage deviceMessage);

void setup()
{
  Serial.begin(115200);

  delay(2000); // delay for 2 seconds

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to WiFi, ssid: " + String(ssid) + +"...");
  }
  Serial.println("+ Connected to WiFi");
  wifiClient.setCACert(root_ca);

  // local address
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Preferences
  preferences.begin("asset-manager" + REVISION, false);
  // preferences.clear();

  // MQTT
  openRemotePubSub.client.setServer(mqtt_host, mqtt_port);
  openRemotePubSub.client.setBufferSize(8192);
  openRemotePubSub.client.setCallback(mqttCallbackHandler);

  // Mutex
  mqttClientMutex = xSemaphoreCreateMutex();

  // Device Manager
  deviceAssetManager.init();

  // Tasks - MQTT and UDP, adjust stack size if needed, ensure main thread is not starved though.
  xTaskCreate(mqttConnectionHandler, "MQTT Connection Task", 12480, NULL, 1, NULL);
  xTaskCreate(udpHandler, "UDP Handler Task", 12480, NULL, 1, NULL);
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
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
    {
      if (!openRemotePubSub.client.connected())
      {
        Serial.println("Connecting to MQTT, host: " + String(mqtt_host) + ", port: " + String(mqtt_port) + ", client id: " + String(mqtt_client_id));
        if (openRemotePubSub.client.connect(mqtt_client_id, mqtt_user, mqtt_pas))
        {
          Serial.println("+ MQTT Connected");

          // Update the gateway status to online (connected)
          openRemotePubSub.updateAttribute("master", gatewayAssetId, "gatewayStatus", "3", false);

          // Subscribe to pending gateway events, we need to acknowledge these events.
          SubscriptionResult result = openRemotePubSub.subscribeToPendingGatewayEvents("master");
          if (result.success)
          {
            Serial.println("+ Subscribed to pending gateway events");
          }

          // Sent our local asset data to OpenRemote, to ensure it is in sync.
          for (int i = 0; i < deviceAssetManager.assets.size(); i++)
          {
            DeviceAsset asset = deviceAssetManager.assets[i];
            if (openRemotePubSub.createAsset("master", asset.managerJson, asset.sn.c_str(), false))
            {
              Serial.println("+ Sent asset data to OpenRemote, sn: " + String(asset.sn.c_str()));
            }
          }
        }
        else
        {
          Serial.println("MQTT Connection Failed");
        }
      }
      xSemaphoreGive(mqttClientMutex);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
// Callback function for MQTT
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received, topic: ");
  Serial.println(topic);

  // Handle response topics
  if (strstr(topic, "response") != NULL)
  {
    Serial.println("+ Response Received");
    // unsubscribe from response topics, part of the request-response pattern
    openRemotePubSub.client.unsubscribe(topic);
    DynamicJsonDocument doc(8096);
    deserializeJson(doc, payload, length);

    // Handle asset events
    bool isAssetEvent = doc["eventType"].as<String>() == "asset";
    bool isCreationEvent = doc["cause"].as<String>() == "CREATE";
    std::string asset = doc["asset"].as<std::string>();

    if (isAssetEvent && isCreationEvent)
    {
      Serial.println("+ Asset Created: " + String(asset.c_str()));
      DeviceAsset deviceAsset = DeviceAsset::fromJson(asset);
      Serial.println("+ Device onboarded: " + String(deviceAsset.sn.c_str()));
      deviceAssetManager.addDeviceAsset(deviceAsset);
    }
  }

  // Handle pending events
  if (strstr(topic, "gateway/events/pending") != NULL)
  {
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
    {
      DynamicJsonDocument doc(8096);
      deserializeJson(doc, payload, length);
      std::string event = doc.as<std::string>();

      Serial.println("+ Pending Gateway Event Received - " + String(event.c_str()));
      if (openRemotePubSub.acknowledgeGatewayEvent(topic))
      {

        Serial.println("+ Acknowledgement Sent");
      }
      xSemaphoreGive(mqttClientMutex);
    }
  }
}

void udpHandler(void *pvParameters)
{
  udp.begin(udp_port);
  while (true)
  {
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
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

          // DATA - used for sending data from devices to the gateway
          if (deviceMessage.message_type == DATA_MESSAGE)
          {
            udpHandleDataMessage(deviceMessage);
          }
          // ALIVE - used for device check, and updating connection details
          if (deviceMessage.message_type == ALIVE_MESSAGE)
          {
            udpHandleAliveMessage(deviceMessage);
          }
          // ONBOARDING - used for onboarding devices locally and on OpenRemote
          if (deviceMessage.message_type == ONBOARD_MESSAGE)
          {
            udpHandleOnboardMessage(deviceMessage);
          }
        }
      }
      xSemaphoreGive(mqttClientMutex);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Alive message handler, used for device check and updating connection details
void udpHandleAliveMessage(DeviceMessage deviceMessage)
{

  // Request onboarding if device is not onboarded
  if (!deviceAssetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_REQ, 11);
    udp.endPacket();
  }
  // Update connection details if device is onboarded
  else
  {
    deviceAssetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());
  }
}

void udpHandleDataMessage(DeviceMessage deviceMessage)
{
  Serial.println("Data received, device: " + String(deviceMessage.toJson().c_str()));

  // Request onboarding if device is not onboarded
  if (!deviceAssetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_REQ, 11);
    udp.endPacket();
  }
  // Handle data messaging
  else
  {
    // Presence data of presence sensor devices
    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      std::string assetId = deviceAssetManager.getDeviceAssetId(deviceMessage.device_sn);
      openRemotePubSub.updateAttribute("master", assetId, "presence", deviceMessage.data, false);
    }

    if (deviceMessage.device_type == ENVIRONMENT_SENSOR_ASSET)
    {
      std::string assetId = deviceAssetManager.getDeviceAssetId(deviceMessage.device_sn);
      // we get json data from the device, we can parse it and update the attributes
      DynamicJsonDocument doc(8096);
      deserializeJson(doc, deviceMessage.data);
      openRemotePubSub.updateAttribute("master", assetId, "temperature", doc["temperature"].as<std::string>(), false);
      openRemotePubSub.updateAttribute("master", assetId, "relativeHumidity", doc["relativeHumidity"].as<std::string>(), false);
    }
  }
}

void udpHandleOnboardMessage(DeviceMessage deviceMessage)
{
  Serial.println("Onboard Message Received, device: " + String(deviceMessage.toJson().c_str()));

  // Check if device is already onboarded in our local asset manager
  if (deviceAssetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    Serial.println("+ Device is onboarded");
    // Send ONBOARDOK back to udp
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_OK, 10);
    udp.endPacket();

    deviceAssetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());
    Serial.println("+ Sent ONBOARD_OK to host: " + udp.remoteIP().toString() + " port: " + udp.remotePort());
    deviceAssetManager.removePendingOnboarding(deviceMessage.device_sn.c_str()); // remove from pending onboarding - we are done.
  }
  // Check if device is pending onboarding, prevent queuing multiple onboarding requests
  else if (deviceAssetManager.isOnboardingPending(deviceMessage.device_sn.c_str()))
  {
    Serial.println("<> Device is pending onboarding");
  }
  // Start onboarding the device.
  else
  {
    // Put the device in the pending onboarding list
    deviceAssetManager.addPendingOnboarding(deviceMessage.device_sn.c_str());

    // Onboard the device with OpenRemote - based on device type, asset templates need to be extended for new types.
    // We can add new asset types in the asset_templates.h file.

    // Plug asset
    if (deviceMessage.device_type == PLUG_ASSET)
    {
      PlugAsset asset = PlugAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      if (openRemotePubSub.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
      {
        Serial.println("+ Sent asset create request");
      }
    }

    // Environment sensor asset
    if (deviceMessage.device_type == ENVIRONMENT_SENSOR_ASSET)
    {
      EnvironmentSensorAsset asset = EnvironmentSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      Serial.println("Asset: " + (String)json.c_str());
      if (openRemotePubSub.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
      {
        Serial.println("+ Sent asset create request");
      }
    }

    // Presence sensor asset
    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      PresenceSensorAsset asset = PresenceSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      if (openRemotePubSub.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
      {
        Serial.println("+ Sent asset create request");
      }
    }
  }
}
