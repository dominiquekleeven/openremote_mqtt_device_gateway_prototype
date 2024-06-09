#include "config/secrets.h"
#include "external/OpenRemotePubSubClient/openremote_pubsub.h"
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "modules/messaging/device_message.h"
#include "modules/manager/asset_manager.h"
#include "modules/manager/asset_templates.h"

using namespace std;

// Simple versioning - used for resetting preferences
#define REVISION 5

// Global Variables
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient); // passed to openRemoteMqtt - which wraps PubSubClient
OpenRemotePubSub openRemoteMqtt(mqtt_client_id, mqttClient);
Preferences preferences;
WiFiUDP udp;
AsyncWebServer server(80);
AssetManager assetManager(preferences);

SemaphoreHandle_t mqttClientMutex;
SemaphoreHandle_t managerMutex;

// Function Prototypes
void mqttConnectionHandler(void *pvParameters);
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length);
void udpHandler(void *pvParameters);
void udpHandleDataMessage(DeviceMessage deviceMessage);
void udpHandleOnboardMessage(DeviceMessage deviceMessage);
void udpHandleAliveMessage(DeviceMessage deviceMessage);
void startWebServer();

unsigned int wifiConnectionAttempts = 0;
unsigned int wifiConnectionAttemptsMax = 10;

void setup()
{
  Serial.begin(115200);

  delay(1000); // delay for 2 seconds

  // SPIFFS
  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print("Connecting to WiFi, ssid: ");
    Serial.println(ssid);
    wifiConnectionAttempts++;

    // Prevent getting stuck in a connection loop, restart if we can't connect.
    if (wifiConnectionAttempts > wifiConnectionAttemptsMax)
    {
      Serial.println("! WiFi connection failed");
      ESP.restart();
    }
  }
  wifiConnectionAttempts = 0;
  Serial.println("+ Connected to WiFi");

  // local address
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // set the CA cert for the WiFi client
  wifiClient.setCACert(root_ca);

  // Web server
  startWebServer();

  // Preferences
  preferences.begin("asset-manager" + REVISION, false);
  // preferences.clear();

  // MQTT
  openRemoteMqtt.client.setServer(mqtt_host, mqtt_port);
  openRemoteMqtt.client.setCallback(mqttCallbackHandler);

  // Mutex
  mqttClientMutex = xSemaphoreCreateMutex();

  // Device Manager
  assetManager.init();
  Serial.println("+ Device manager initialized");
  Serial.print("Asset count: ");
  Serial.println(assetManager.assets.size());

  // Tasks - MQTT and UDP, adjust stack size if needed, ensure main thread is not starved though.
  xTaskCreate(mqttConnectionHandler, "MQTT Connection Task", 34816, NULL, 1, NULL); // 34KB stack size, recommended with SSL
  xTaskCreate(udpHandler, "UDP Handler Task", 12480, NULL, 1, NULL);                // 12KB stack size
}

unsigned long lastReconnectAttempt = 0;
unsigned long lastReconnectAttemptInterval = 5000;
unsigned long lastSystemStatusUpdate = 0;
unsigned long lastSystemStatusUpdateInterval = 10000;

// Core Loop
void loop()
{
  // WiFi reconnect - if disconnected
  if (WiFi.status() != WL_CONNECTED && (millis() - lastReconnectAttempt) > lastReconnectAttemptInterval)
  {
    Serial.println("! WiFi disconnected");
    WiFi.reconnect();
    lastReconnectAttempt = millis();
  }

  openRemoteMqtt.client.loop();
  delay(100);
}

// MQTT Task
void mqttConnectionHandler(void *pvParameters)
{
  while (true)
  {
    // Ensure we have the mutex
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
    {
      // Reconnect to MQTT if disconnected
      if (!openRemoteMqtt.client.connected() && WiFi.status() == WL_CONNECTED)
      {
        Serial.print("Connecting to MQTT, host: ");
        Serial.print(mqtt_host);
        Serial.print(", port: ");
        Serial.println(mqtt_port);

        if (openRemoteMqtt.client.connect(mqtt_client_id, mqtt_user, mqtt_pas))
        {
          Serial.println("+ MQTT connected");

          // Update the gateway status to online (connected)
          openRemoteMqtt.updateAttribute("master", gatewayAssetId, "gatewayStatus", "3", false);

          // Subscribe to pending gateway events, we need to acknowledge these events.
          if (openRemoteMqtt.subscribeToPendingGatewayEvents("master"))
          {
            Serial.println("+ Subscribed to pending gateway events");
          }

          // Sent our local asset data to OpenRemote, to ensure it is in sync.
          for (int i = 0; i < assetManager.assets.size(); i++)
          {
            DeviceAsset asset = assetManager.assets[i];
            if (openRemoteMqtt.createAsset("master", asset.managerJson, asset.sn.c_str(), false))
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

      // Update the gateway status to online (connected) - constant update, ensuring the status is always correct.
      if (openRemoteMqtt.client.connected() && WiFi.status() == WL_CONNECTED && (millis() - lastSystemStatusUpdate) > lastSystemStatusUpdateInterval)
      {
        lastSystemStatusUpdate = millis();
        openRemoteMqtt.updateAttribute("master", gatewayAssetId, "gatewayStatus", "3", false);
      }

      // Give the mutex back
      xSemaphoreGive(mqttClientMutex);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS); // we check every 2 seconds, adjust if needed.
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
    // Grab the mutex, we are going to access the mqtt client
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
    {
      Serial.println("Request response received");
      // unsubscribe from response topics, part of the request-response pattern
      openRemoteMqtt.client.unsubscribe(topic);
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload, length);

      // Handle asset events
      bool isAssetEvent = doc["eventType"].as<std::string>() == "asset";
      bool isCreationEvent = doc["cause"].as<std::string>() == "CREATE";
      std::string asset = doc["asset"].as<std::string>();

      if (isAssetEvent && isCreationEvent)
      {
        DeviceAsset deviceAsset = DeviceAsset::fromJson(asset);
        Serial.print("+ Device onboarded, data: ");
        Serial.println(deviceAsset.managerJson.c_str());
        assetManager.addDeviceAsset(deviceAsset);
      }
      // Give the mutex back
      xSemaphoreGive(mqttClientMutex);
    }
  }

  // Handle pending events
  if (strstr(topic, "gateway/events/pending") != NULL)
  {
    // Grab the mutex, we are going to access the mqtt client
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
    {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload, length);
      std::string event = doc.as<std::string>();

      Serial.println("Pending gateway event received:");
      Serial.println(event.c_str());

      if (openRemoteMqtt.acknowledgeGatewayEvent(topic))
      {
        Serial.println("+ Pending event acknowledged");
      }

      // Give the mutex back
      xSemaphoreGive(mqttClientMutex);
    }
  }
}

void udpHandler(void *pvParameters)
{
  udp.begin(udp_port);
  while (true)
  {
    // Ensure we have the mutex, we are going to access the mqtt client

    // Ensure we are connected to OpenRemote and WiFi
    if (WiFi.status() == WL_CONNECTED)
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
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Alive message handler, used for device check and updating connection details
void udpHandleAliveMessage(DeviceMessage deviceMessage)
{

  // Request onboarding if device is not onboarded
  if (!assetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_REQ, 11);
    udp.endPacket();
  }
  // Update connection details if device is onboarded
  else
  {
    assetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());
  }
}

void udpHandleDataMessage(DeviceMessage deviceMessage)
{
  Serial.print("Device data received - data: ");
  Serial.println(deviceMessage.data.c_str());

  // Request onboarding if device is not onboarded
  if (!assetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
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
      std::string assetId = assetManager.getDeviceAssetId(deviceMessage.device_sn);
      // get the mutex cause we are going to access the mqtt client
      if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
      {
        openRemoteMqtt.updateAttribute("master", assetId, "presence", deviceMessage.data, false);
        // give the mutex back
        xSemaphoreGive(mqttClientMutex);
      }
    }

    if (deviceMessage.device_type == ENVIRONMENT_SENSOR_ASSET)
    {
      std::string assetId = assetManager.getDeviceAssetId(deviceMessage.device_sn);
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, deviceMessage.data);
      // get the mutex cause we are going to access the mqtt client
      if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
      {
        openRemoteMqtt.updateAttribute("master", assetId, "temperature", doc["temperature"].as<std::string>(), false);
        openRemoteMqtt.updateAttribute("master", assetId, "relativeHumidity", doc["relativeHumidity"].as<std::string>(), false);
        // give the mutex back
        xSemaphoreGive(mqttClientMutex);
      }
    }
  }
}

void udpHandleOnboardMessage(DeviceMessage deviceMessage)
{
  // Check if device is already onboarded in our local asset manager
  if (assetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    Serial.println("Device is onboarded");
    // Send ONBOARDOK back to udp
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_OK, 10);
    udp.endPacket();

    assetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());

    Serial.print("+ Sent ONBOARD_OK to host: ");
    Serial.print(udp.remoteIP());
    Serial.print(", port: ");
    Serial.println(udp.remotePort());

    assetManager.removePendingOnboarding(deviceMessage.device_sn.c_str()); // remove from pending onboarding - we are done.
  }
  // Check if device is pending onboarding, prevent queuing multiple onboarding requests
  else if (assetManager.isOnboardingPending(deviceMessage.device_sn.c_str()))
  {
    Serial.println("Device is pending onboarding");
  }
  // Start onboarding the device.
  else
  {
    // Put the device in the pending onboarding list
    assetManager.addPendingOnboarding(deviceMessage.device_sn.c_str());

    // Onboard the device with OpenRemote - based on device type, asset templates need to be extended for new types.
    // We can add new asset types in the asset_templates.h file.

    // Plug asset
    if (deviceMessage.device_type == PLUG_ASSET)
    {
      PlugAsset asset = PlugAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();

      // get the mutex cause we are going to access the mqtt client
      if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the mutex back
        xSemaphoreGive(mqttClientMutex);
      }
    }

    // Environment sensor asset
    if (deviceMessage.device_type == ENVIRONMENT_SENSOR_ASSET)
    {
      EnvironmentSensorAsset asset = EnvironmentSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      // get the mutex cause we are going to access the mqtt client
      if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the mutex back
        xSemaphoreGive(mqttClientMutex);
      }
    }

    // Presence sensor asset
    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      PresenceSensorAsset asset = PresenceSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      // get the mutex cause we are going to access the mqtt client
      if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the mutex back
        xSemaphoreGive(mqttClientMutex);
      }
    }
  }
}

void startWebServer()
{
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // View page of an asset
  server.on("/view", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if(request->hasParam("id")){
      String id = request->getParam("id")->value();
      request->send(SPIFFS, "/view.html", "text/html");
    } else {
      request->send(404, "text/plain", "404: Not Found");
    } });

  // Endpoint for retrieving list of assets
  server.on("/manager/assets", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("id"))
    {
      String id = request->getParam("id")->value();
      DeviceAsset asset = assetManager.getDeviceAssetById(id.c_str());
      if (asset.id == "")
      {
        request->send(404, "application/json", "{\"status\": \"error\"}");
      }
      else
      {
        DynamicJsonDocument doc(4096);
        doc["sn"] = asset.sn;
        doc["type"] = asset.type;
        doc["id"] = asset.id;
        doc["managerJson"] = asset.managerJson;

        std::string output;
        serializeJson(doc, output);
        request->send(200, "application/json", output.c_str());
      }
    }
    else
    {
      DynamicJsonDocument doc(8192);
      JsonArray assets = doc.createNestedArray("assets");

      for (int i = 0; i < assetManager.assets.size(); i++)
      {
        DeviceAsset asset = assetManager.assets[i];
        JsonObject assetJson = assets.createNestedObject();
        assetJson["sn"] = asset.sn;
        assetJson["type"] = asset.type;
        assetJson["id"] = asset.id;
      }
      std::string output;
      serializeJson(doc, output);
      request->send(200, "application/json", output.c_str());
    } });

  // Delete asset endpoint
  server.on("/manager/assets", HTTP_DELETE, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("id"))
    {
      String id = request->getParam("id")->value();
     // take the mutex cause we are going to access the mqtt client
    if (xSemaphoreTake(mqttClientMutex, portMAX_DELAY) == pdTRUE)
    {
      if (openRemoteMqtt.deleteAsset("master", id.c_str()))
      {
        if (assetManager.deleteDeviceAssetById(id.c_str()))
        {
          request->send(200, "application/json", "{\"status\": \"ok\"}");
        }
        else
        {
          request->send(500, "application/json", "{\"status\": \"error\"}");
        }
      }
      // give the mutex back
      xSemaphoreGive(mqttClientMutex);
    }
    else
    {
      request->send(404, "application/json", "{\"status\": \"error\"}");
    }
    } });

  // Start the server
  server.begin();
}
