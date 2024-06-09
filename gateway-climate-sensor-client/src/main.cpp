#include "config/secrets.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "device_message.h"
#include <DHT.h>

int DHT_PIN = D1;
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

WiFiUDP udp;
int lastMotionState = LOW; // last PIR state

bool onBoarding = true; // Always send onboarding message on startup

const char *deviceName = "Humidity & Temperature Sensor"; // Name of the device
const char *serialNumber = "PB10A-ORLZ1";                 // Serial number of the device
const char *deviceType = "EnvironmentSensorAsset";        // Type of the device

void setup()
{
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  udp.begin(udpPort);
  dht.begin();
  Serial.println("UDP connection started");
}

// onboarding millis
unsigned long onboardingMillis = 0;
unsigned long measurementMillis = 0;
unsigned long measurementInterval = 60000; // 60s
float lastTemperatureMeasurement = 0;
float lastHumidityMeasurement = 0;

void loop()
{
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (!onBoarding && millis() - measurementMillis > measurementInterval)
  {
    lastHumidityMeasurement = humidity;
    lastTemperatureMeasurement = temperature;
    measurementMillis = millis();

    // Create a message
    std::string data = "{\"temperature\":" + std::to_string(lastTemperatureMeasurement) + ",\"relativeHumidity\":" + std::to_string(lastHumidityMeasurement) + "}";
    DeviceMessage deviceMessage = DeviceMessage(deviceName, serialNumber, deviceType, data, MessageType::DATA_MESSAGE);

    // Send the message
    udp.beginPacket(udpServer, udpPort);
    std::string message = deviceMessage.toJson();
    udp.write(message.c_str(), message.length());
    udp.endPacket();

    Serial.println("Sent message: " + String(message.c_str()) + " to " + udpServer + ":" + udpPort);
  }

  if (onBoarding && millis() - onboardingMillis > 5000) // Send onboarding message every 5 seconds
  {
    onboardingMillis = millis();
    DeviceMessage deviceMessage = DeviceMessage(deviceName, serialNumber, deviceType, "", MessageType::ONBOARD_MESSAGE);

    // Send the message
    udp.beginPacket(udpServer, udpPort);
    std::string message = deviceMessage.toJson();
    udp.write(message.c_str(), message.length());
    udp.endPacket();

    Serial.println("Sent onboarding message: " + String(message.c_str()) + " to " + udpServer + ":" + udpPort);
  }

  // Check for incoming messages
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    char packetBuffer[255];
    udp.read(packetBuffer, packetSize);
    packetBuffer[packetSize] = '\0';
    Serial.println("Received packet: " + String(packetBuffer));

    if (String(packetBuffer) == "ONBOARD_OK")
    {
      onBoarding = false; // Onboarding is complete
      Serial.println("Onboarding complete");
    }

    if (String(packetBuffer) == "ONBOARD_REQ")
    {
      onBoarding = true; // Gateway is requesting onboarding
      Serial.println("Onboarding started");
    }
  }

  // Add a small delay, don't need to check the PIR sensor every millisecond
  delay(100);
}