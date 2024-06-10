#include "config/secrets.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "device_message.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"

WiFiUDP udp;
int lastMotionState = LOW; // last PIR state

bool onBoarding = true; // Always send onboarding message on startup

const char *deviceName = "Air Quality Sensor";    // Name of the device
const char *serialNumber = "PI1MA-Q20M1";         // Serial number of the device
const char *deviceType = "AirQualitySensorAsset"; // Type of the device

Adafruit_BME680 bme; // I2C

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
  bme.begin();

  bme.begin();
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);
  Serial.println("UDP connection started");
}

unsigned long onboardingMillis = 0;
unsigned long measurementMillis = 0;
unsigned long measurementInterval = 30000; // 30 seconds

void loop()
{
  bme.performReading();
  float temperature = bme.temperature;
  float humidity = bme.humidity;
  float pressure = bme.pressure / 100.0;
  float gas = bme.gas_resistance / 1000.0;
  float altitude = bme.readAltitude(1013.25);

  if (!onBoarding && millis() - measurementMillis > measurementInterval)
  {
    measurementMillis = millis();
    JsonDocument data;
    data["temperature"] = temperature;
    data["humidity"] = humidity;
    data["pressure"] = pressure;
    data["gas"] = gas;
    data["altitude"] = altitude;

    DeviceMessage deviceMessage = DeviceMessage(deviceName, serialNumber, deviceType, data.as<std::string>(), MessageType::DATA_MESSAGE);

    // Send the message
    udp.beginPacket(udpServer, udpPort);
    std::string message = deviceMessage.toJson();
    udp.write(message.c_str(), message.length());
    udp.endPacket();

    Serial.println("Sent data message: " + String(message.c_str()) + " to " + udpServer + ":" + udpPort);
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

  // Add a small delay, don't need to check the sensor every millisecond
  delay(2000);
}