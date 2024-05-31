# OpenRemote Device Gateway Prototype
This repository contains a prototype that functions as a IoT Device Gateway, its purpose and scope is to validate the functionality of the Gateway MQTT API interface of the OpenRemote platform.

### Repository contents
- ```device-gateway```: The prototype source code
- ```gateway-motion-sensor-client```: Source code for a client that publishes motion data over UDP
- ```gateway-climate-sensor-client```: Source code for a client that publishes climate ```(temperature, humidity)``` data over UDP
- ```OpenRemotePubSubClient```: Wrapper library for the MQTT Gateway API topics, depends on the PubSubClient library.

### IDE
This project uses [PlatformIO](https://platformio.org/) for its development environment
***

### Hardware Setup
> ```Device Gateway```
- T-Relay S3 (ESP32 S3) - [Product documentation](https://github.com/Xinyuan-LilyGO/LilyGo-T-Relay/blob/main/docs/RELAY_ESP32S3.MD)
- Physical Light (controlled through the Relay)
> ```Motion Sensor ```
- Wemos D1 Mini (ESP8266)
- PIR Sensor
> ```Climate Sensor``` 
- Wemos D1 Mini (ESP8266)
- DHT22 Sensor


### Software  Setup
 > ```Device Gateway```
- Arduino Framework
- Additionally leverages FreeRTOS for multi-tasking and Preferences for storing data locally.
> ```Motion Sensor Client```
- Arduino Framework
> ```Climate Sensor Client```
- Arduino Framework



