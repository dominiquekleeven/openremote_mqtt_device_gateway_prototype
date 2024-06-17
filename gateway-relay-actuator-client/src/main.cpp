#include "config/secrets.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "device_message.h"

int RELAY_PIN = D1;
WiFiUDP udp;
int initialRelayState = LOW;

bool onBoarding = true; // Always send onboarding message on startup

const char *deviceName = "Relay Switch";  // Name of the device
const char *serialNumber = "Z02RL-ARKXF"; // Serial number of the device
const char *deviceType = "PlugAsset";     // Type of the device

// keepalivemillis
unsigned long keepAliveMillis = 0;
unsigned long keepAliveInterval = 10000; // Send keep alive message every 10 seconds

void setup()
{
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, initialRelayState);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  udp.begin(udpPort);
  Serial.println("UDP connection started");
}

// onboarding millis
unsigned long onboardingMillis = 0;

void loop()
{

  if (onBoarding && millis() - onboardingMillis > 5000) // Send onboarding message every 5 seconds
  {
    onboardingMillis = millis();
    DeviceMessage onboardMessage = DeviceMessage(deviceName, serialNumber, deviceType, "", MessageType::ONBOARD_MESSAGE);
    // Send the message
    udp.beginPacket(udpServer, udpPort);
    std::string message = onboardMessage.toJson();
    udp.write(message.c_str(), message.length());
    udp.endPacket();

    Serial.println("Sent onboarding message: " + String(message.c_str()) + " to " + udpServer + ":" + udpPort);
  }

  if (!onBoarding && millis() - keepAliveMillis > keepAliveInterval)
  {
    keepAliveMillis = millis();
    DeviceMessage keepAliveMessage = DeviceMessage(deviceName, serialNumber, deviceType, "", MessageType::ALIVE_MESSAGE);
    // Send the message
    udp.beginPacket(udpServer, udpPort);
    std::string message = keepAliveMessage.toJson();
    udp.write(message.c_str(), message.length());
    udp.endPacket();

    Serial.println("Sent keep alive message: " + String(message.c_str()) + " to " + udpServer + ":" + udpPort);
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

    if (String(packetBuffer) == "ACTION_ON")
    {
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("Toggled relay to " + String(initialRelayState));
    }

    if (String(packetBuffer) == "ACTION_OFF")
    {
      digitalWrite(RELAY_PIN, LOW);
      Serial.println("Toggled relay to " + String(initialRelayState));
    }
  }

  delay(10);
}