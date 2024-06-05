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
    Serial.print("Connecting to WiFi, ssid: ");
    Serial.println(ssid);
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
  openRemotePubSub.client.setCallback(mqttCallbackHandler);

  // Mutex
  mqttClientMutex = xSemaphoreCreateMutex();

  // Device Manager
  deviceAssetManager.init();
  Serial.println("+ Device manager initialized");
  Serial.print("Asset count: ");
  Serial.println(deviceAssetManager.assets.size());

  // Tasks - MQTT and UDP, adjust stack size if needed, ensure main thread is not starved though.
  xTaskCreate(mqttConnectionHandler, "MQTT Connection Task", 34816, NULL, 1, NULL); // 34KB stack size, recommended with SSL
  xTaskCreate(udpHandler, "UDP Handler Task", 12480, NULL, 1, NULL);                // 12KB stack size
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
        Serial.print("Connecting to MQTT, host: ");
        Serial.print(mqtt_host);
        Serial.print(", port: ");
        Serial.println(mqtt_port);

        if (openRemotePubSub.client.connect(mqtt_client_id, mqtt_user, mqtt_pas))
        {
          Serial.println("+ MQTT connected");

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
              Serial.print("+ Sent asset data to OpenRemote, sn: ");
              Serial.println(asset.sn.c_str());
            }
          }
        }
        else
        {
          Serial.println("! MQTT connection failed");
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
    Serial.println("Request response received");
    // unsubscribe from response topics, part of the request-response pattern
    openRemotePubSub.client.unsubscribe(topic);
    DynamicJsonDocument doc(8096);
    deserializeJson(doc, payload, length);

    // Handle asset events
    bool isAssetEvent = doc["eventType"].as<std::string>() == "asset";
    bool isCreationEvent = doc["cause"].as<std::string>() == "CREATE";
    std::string asset = doc["asset"].as<std::string>();

    if (isAssetEvent && isCreationEvent)
    {
      DeviceAsset deviceAsset = DeviceAsset::fromJson(asset);
      Serial.print("+ Device onboarded, sn: ");
      Serial.println(deviceAsset.sn.c_str());
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

      Serial.println("Pending gateway event received:");
      Serial.println(event.c_str());

      if (openRemotePubSub.acknowledgeGatewayEvent(topic))
      {
        Serial.println("+ Pending event acknowledged");
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
  Serial.print("Device data received - data: ");
  Serial.println(deviceMessage.data.c_str());

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
      DynamicJsonDocument doc(8096);
      deserializeJson(doc, deviceMessage.data);
      openRemotePubSub.updateAttribute("master", assetId, "temperature", doc["temperature"].as<std::string>(), false);
      openRemotePubSub.updateAttribute("master", assetId, "relativeHumidity", doc["relativeHumidity"].as<std::string>(), false);
    }
  }
}

void udpHandleOnboardMessage(DeviceMessage deviceMessage)
{
  // Check if device is already onboarded in our local asset manager
  if (deviceAssetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    Serial.println("Device is onboarded");
    // Send ONBOARDOK back to udp
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_OK, 10);
    udp.endPacket();

    deviceAssetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());

    Serial.print("+ Sent ONBOARD_OK to host: ");
    Serial.print(udp.remoteIP());
    Serial.print(", port: ");
    Serial.println(udp.remotePort());

    deviceAssetManager.removePendingOnboarding(deviceMessage.device_sn.c_str()); // remove from pending onboarding - we are done.
  }
  // Check if device is pending onboarding, prevent queuing multiple onboarding requests
  else if (deviceAssetManager.isOnboardingPending(deviceMessage.device_sn.c_str()))
  {
    Serial.println("Device is pending onboarding");
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
