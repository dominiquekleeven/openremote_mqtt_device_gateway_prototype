
// External includes, libraries
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// Internal includes
#include "config/secrets.h"
#include "external/OpenRemotePubSubClient/openremote_pubsub.h"
#include "modules/messaging/device_message.h"
#include "modules/manager/asset_manager.h"
#include "modules/manager/asset_templates.h"
#include <map>

using namespace std;

// Simple versioning - used for resetting preferences
#define REVISION 5

// Global Variables
WiFiClientSecure wifiClient;                                 // WiFi client for secure connections
PubSubClient mqttClient(wifiClient);                         // passed to openRemoteMqtt - which wraps PubSubClient
OpenRemotePubSub openRemoteMqtt(mqtt_client_id, mqttClient); // OpenRemote PubSub client
Preferences preferences;                                     // Preferences for storing asset data (non-volatile memory)
WiFiUDP udp;                                                 // UDP for local device communication
AsyncWebServer server(80);                                   // Management interface
AssetManager assetManager(preferences);                      // Asset manager

// Semaphore
SemaphoreHandle_t pubSubSemaphore; // Semaphore for accessing the mqtt client

// Function Prototypes
void mqttConnectionHandler(void *pvParameters);
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length);
void udpHandler(void *pvParameters);
void udpHandleDataMessage(DeviceMessage deviceMessage);
void udpHandleOnboardMessage(DeviceMessage deviceMessage);
void udpHandleAliveMessage(DeviceMessage deviceMessage);
void startWebServer();

// Global Variables
unsigned int wifiConnectionAttempts = 0;
unsigned int wifiConnectionAttemptsMax = 10;

void setup()
{
  Serial.begin(115200);

  // Initialize SPIFFS (file system for web server)
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // WiFi connection
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print("Connecting to WiFi, ssid: ");
    Serial.println(ssid);
    wifiConnectionAttempts++;

    if (wifiConnectionAttempts > wifiConnectionAttemptsMax)
    {
      Serial.println("! WiFi connection failed");
      ESP.restart();
    }
  }
  wifiConnectionAttempts = 0;
  Serial.println("+ WiFi");

  // local address
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  wifiClient.setCACert(root_ca);

  // Preferences, used for storing asset data
  preferences.begin("asset-manager" + REVISION, false);

  // MQTT client
  openRemoteMqtt.client.setServer(mqtt_host, mqtt_port);
  openRemoteMqtt.client.setCallback(mqttCallbackHandler);

  // semaphore for accessing the mqtt client
  pubSubSemaphore = xSemaphoreCreateMutex();

  // Asset manager, load assets from preferences
  assetManager.init();
  Serial.println("+ Device manager initialized");
  Serial.print("Asset count: ");
  Serial.println(assetManager.assets.size());

  // Web server, simple management interface
  startWebServer();

  // FreeRTOS tasks
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
    // Ensure we have the semaphore
    if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
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
          if (openRemoteMqtt.subscribeToPendingGatewayEvents("master"))
          {
            Serial.println("+ Subscribed to pending gateway events");
          }

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

      if (openRemoteMqtt.client.connected() && WiFi.status() == WL_CONNECTED && (millis() - lastSystemStatusUpdate) > lastSystemStatusUpdateInterval)
      {
        lastSystemStatusUpdate = millis();
      }
      xSemaphoreGive(pubSubSemaphore);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}
// Callback function for MQTT, handles incoming messages
void mqttCallbackHandler(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received, topic: ");
  Serial.println(topic);

  // Handle response topics
  if (strstr(topic, "response") != NULL)
  {
    Serial.println("Request response received");
    // Grab the semaphore, we are going to access the mqtt client
    if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
    {
      // unsubscribe from response topics, part of the request-response pattern
      openRemoteMqtt.client.unsubscribe(topic);
      xSemaphoreGive(pubSubSemaphore);
    }

    JsonDocument doc;
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
  }

  // Handle pending events
  if (strstr(topic, "gateway/events/pending") != NULL)
  {
    JsonDocument doc;
    deserializeJson(doc, payload, length);
    std::string event = doc.as<std::string>();

    Serial.println("Pending gateway event received:");

    std::string ackId = doc["ackId"].as<std::string>();
    bool isAttributeEvent = doc["event"]["eventType"].as<std::string>() == "attribute";
    std::string assetId = doc["event"]["ref"]["id"].as<std::string>();
    std::string eventValue = doc["event"]["value"].as<std::string>();
    std::string eventAttribute = doc["event"]["ref"]["name"].as<std::string>();

    Serial.print("Asset ID: ");
    Serial.println(assetId.c_str());
    Serial.print("Event attribute: ");
    Serial.println(eventAttribute.c_str());
    Serial.print("Event value: ");
    Serial.println(eventValue.c_str());

    // Handle the event
    if (isAttributeEvent)
    {
      DeviceAsset deviceAsset = assetManager.getDeviceAssetById(assetId.c_str());

      // Cant handle the event if the asset is not found
      if (deviceAsset.id == "")
      {
        return;
      }

      // PlugAsset has a control attribute "onOff"
      if (deviceAsset.type == PLUG_ASSET)
      {
        if (eventAttribute == "onOff")
        {
          std::string action = eventValue == "true" ? ACTION_ON : ACTION_OFF;
          udp.beginPacket(deviceAsset.address, deviceAsset.port);
          udp.write((const uint8_t *)action.c_str(), action.length());
          udp.endPacket();
        }
      }

      // Acknowledge the event
      // Grab the semaphore, we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.acknowledgeGatewayEvent("master", ackId))
        {
          Serial.println("+ Pending event acknowledged");
        }
        // Give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }
  }
}

void udpHandler(void *pvParameters)
{
  udp.begin(udp_port);
  while (true)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      int packetSize = udp.parsePacket();
      if (packetSize)
      {
        char incomingPacket[255];
        udp.read(incomingPacket, 255);
        incomingPacket[packetSize] = 0;
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
  if (!assetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_REQ, 11);
    udp.endPacket();
  }
  else
  {
    assetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());
  }
}

void udpHandleDataMessage(DeviceMessage deviceMessage)
{
  Serial.print("Device data received - data: ");
  Serial.println(deviceMessage.data.c_str());

  if (!assetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_REQ, 11);
    udp.endPacket();
  }
  else
  {
    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      std::string assetId = assetManager.getDeviceAssetId(deviceMessage.device_sn);
      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        openRemoteMqtt.updateAttribute("master", assetId, "presence", deviceMessage.data, false);
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }

    if (deviceMessage.device_type == ENVIRONMENT_SENSOR_ASSET)
    {
      std::string assetId = assetManager.getDeviceAssetId(deviceMessage.device_sn);
      JsonDocument doc;
      deserializeJson(doc, deviceMessage.data);
      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        openRemoteMqtt.updateAttribute("master", assetId, "temperature", doc["temperature"].as<std::string>(), false);
        openRemoteMqtt.updateAttribute("master", assetId, "relativeHumidity", doc["relativeHumidity"].as<std::string>(), false);
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }

    if (deviceMessage.device_type == AIR_QUALITY_SENSOR_ASSET)
    {
      std::string assetId = assetManager.getDeviceAssetId(deviceMessage.device_sn);
      JsonDocument doc;
      deserializeJson(doc, deviceMessage.data);
      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        JsonDocument attributeTemplateDoc;
        attributeTemplateDoc["temperature"] = doc["temperature"].as<std::string>();
        attributeTemplateDoc["humidity"] = doc["humidity"].as<std::string>();
        attributeTemplateDoc["gasResistance"] = doc["gas"].as<std::string>();
        attributeTemplateDoc["altitude"] = doc["altitude"].as<std::string>();
        attributeTemplateDoc["pressure"] = doc["pressure"].as<std::string>();

        openRemoteMqtt.updateMultipleAttributes("master", assetId, attributeTemplateDoc.as<std::string>(), false);
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }
  }
}

void udpHandleOnboardMessage(DeviceMessage deviceMessage)
{
  if (assetManager.isDeviceOnboarded(deviceMessage.device_sn.c_str()))
  {
    Serial.println("Device is onboarded");
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write((const uint8_t *)ONBOARD_OK, 10);
    udp.endPacket();

    // Update the connection details
    assetManager.setConnection(deviceMessage.device_sn.c_str(), udp.remoteIP(), udp.remotePort());

    Serial.print("+ Sent ONBOARD_OK to host: ");
    Serial.print(udp.remoteIP());
    Serial.print(", port: ");
    Serial.println(udp.remotePort());

    assetManager.removePendingOnboarding(deviceMessage.device_sn.c_str()); // remove from pending onboarding - we are done.
  }
  else if (assetManager.isOnboardingPending(deviceMessage.device_sn.c_str()))
  {
    Serial.println("Device is pending onboarding");
  }
  else
  {
    assetManager.addPendingOnboarding(deviceMessage.device_sn.c_str());
    if (deviceMessage.device_type == PLUG_ASSET)
    {
      PlugAsset asset = PlugAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();

      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }

    if (deviceMessage.device_type == ENVIRONMENT_SENSOR_ASSET)
    {
      EnvironmentSensorAsset asset = EnvironmentSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }

    if (deviceMessage.device_type == AIR_QUALITY_SENSOR_ASSET)
    {
      AirQualitySensorAsset asset = AirQualitySensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }

    if (deviceMessage.device_type == PRESENCE_SENSOR_ASSET)
    {
      PresenceSensorAsset asset = PresenceSensorAsset(deviceMessage.device_name.c_str(), deviceMessage.device_sn.c_str(), deviceMessage.device_type.c_str());
      std::string json = asset.toJson();
      // get the semaphore cause we are going to access the mqtt client
      if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
      {
        if (openRemoteMqtt.createAsset("master", json, deviceMessage.device_sn.c_str(), true))
        {
          Serial.println("+ Sent asset create request");
        }
        // give the semaphore back
        xSemaphoreGive(pubSubSemaphore);
      }
    }
  }
}

// HTTP Request Buffers, large requests are split into multiple packets (This is default behavior for HTTP)
std::map<String, std::vector<uint8_t>> requestBuffers;

// Start the web server
// - /: serves index.html
// - /view?id=xxxxx: view page of an asset
// - /manager/assets: GET: list of assets, GET ?id=xxxxx, DELETE ?id=xxxxx, PUT ?id=xxxxx
// - /system/status: GET: system status (ip, heap, uptime)
void startWebServer()
{
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // Serve asset view page
  server.on("/view", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        if (request->hasParam("id"))
        {
            String id = request->getParam("id")->value();
            request->send(SPIFFS, "/view.html", "text/html");
        }
        else
        {
            request->send(404, "text/plain", "404: Not Found");
        } });

  // List - Single asset endpoint
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
                JsonDocument doc;
                doc["sn"] = asset.sn;
                doc["type"] = asset.type;
                doc["id"] = asset.id;
                doc["managerJson"] = asset.managerJson;

                std::string output;
                ArduinoJson::serializeJson(doc, output);
                request->send(200, "application/json", output.c_str());
            }
        }
        else
        {
            JsonDocument doc;
            JsonArray assets = doc["assets"].to<JsonArray>();

            for (int i = 0; i < assetManager.assets.size(); i++)
            {
                DeviceAsset asset = assetManager.assets[i];
                JsonObject assetJson = assets.add<JsonObject>();
                assetJson["sn"] = asset.sn;
                assetJson["type"] = asset.type;
                assetJson["id"] = asset.id;
            }
            std::string output;
            ArduinoJson::serializeJson(doc, output);
            request->send(200, "application/json", output.c_str());
        } });

  // Delete asset endpoint
  server.on("/manager/assets", HTTP_DELETE, [](AsyncWebServerRequest *request)
            {
        if (request->hasParam("id"))
        {
            String id = request->getParam("id")->value();
            // take the semaphore because we are going to access the mqtt client
            if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
            {
                if (assetManager.deleteDeviceAssetById(id.c_str()))
                {
                    openRemoteMqtt.deleteAsset("master", id.c_str());
                    request->send(200, "application/json", "{\"status\": \"ok\"}");
                }
                else
                {
                    request->send(500, "application/json", "{\"status\": \"error\"}");
                }
                // give the semaphore back
                xSemaphoreGive(pubSubSemaphore);
            }
            else
            {
                request->send(404, "application/json", "{\"status\": \"error\"}");
            }
        } });

  // Update asset endpoint, uses buffer to store large requests
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
                       {
        if (request->hasParam("id") && request->url() == "/manager/assets" && request->method() == HTTP_PUT)
        {
            String id = request->getParam("id")->value();
            if (index == 0)
            {
                requestBuffers[id].clear();
            }
            requestBuffers[id].insert(requestBuffers[id].end(), data, data + len);

            if (index + len == total)
            {
                std::vector<uint8_t>& buffer = requestBuffers[id];
                JsonDocument doc;
                ArduinoJson::deserializeJson(doc, buffer.data(), buffer.size());
                std::string json = doc.as<std::string>();

                // take the semaphore because we are going to access the mqtt client
                if (xSemaphoreTake(pubSubSemaphore, portMAX_DELAY) == pdTRUE)
                {
                    if (assetManager.updateDeviceAssetJson(id.c_str(), json.c_str()))
                    {
                        openRemoteMqtt.updateAsset("master", id.c_str(), json.c_str());
                        request->send(200, "application/json", "{\"status\": \"ok\"}");
                    }
                    else
                    {
                        request->send(500, "application/json", "{\"status\": \"error\"}");
                    }
                    // give the semaphore back
                    xSemaphoreGive(pubSubSemaphore);
                }
                else
                {
                    request->send(404, "application/json", "{\"status\": \"error\"}");
                }
                // Clear the buffer after processing
                requestBuffers.erase(id);
            }
        } });

  // Endpoint to get local IP + free heap space + uptime
  server.on("/system/status", HTTP_GET, [](AsyncWebServerRequest *request)
            {
        JsonDocument doc;
        doc["ip"] = WiFi.localIP();
        doc["heap"] = ESP.getFreeHeap() / 1024;
        doc["uptime"] = millis() / 1000;
        std::string output;
        ArduinoJson::serializeJson(doc, output);
        request->send(200, "application/json", output.c_str()); });

  // Start the server
  server.begin();
}
