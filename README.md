![CleanShot 2024-06-10 at 20 46 01@2x](https://github.com/dominiquekleeven/openremote_mqtt_device_gateway_prototype/assets/10584854/8bc0756d-3bd1-4521-8296-8f961e6adbfa)
![image](https://github.com/user-attachments/assets/1d559705-0d14-4807-9b58-9c1b12fab1c6)


# OpenRemote Device Gateway Prototype
This repository contains a **prototype** that functions as a IoT Device Gateway (or Edge Gateway), its purpose and scope is to validate the functionality of the Gateway MQTT API interface of the OpenRemote platform. 
> Please note that it is a prototype and not intended for production usage, its main purpose is as a testing device and integration example.

### Repository contents
- ```device-gateway```: The prototype source code
- ```gateway-motion-sensor-client```: Source code for a client that publishes motion data over UDP
- ```gateway-climate-sensor-client```: Source code for a client that sends climate ```(temperature, humidity)``` data over UDP
- ```gateway-airquality-sensor-client```: Source code for a client that sends air quality data ```{temperature, humidity, gasResistance, altitude, pressure}``` over UDP
- ```gateway-relay-actuator-client```: Source code for a client that can be controlled over UDP, allowing the toggling of a relay.

### IDE
This project uses [PlatformIO](https://platformio.org/) for its development environment, this includes dependency management as well.
***


### Flashing information
- The device gateway uses SPIFFS for serving webcontent, when using VSCode + PlatformIO you can simply upload the system image, which will transfer the contents from the data folder.
- For the clients, adjust the PIN constants with your wiring.

### Devices
> ```IoT Device Gateway Prototype```
- ESP32 (Generic 4MB Flash, 512KB SRAM)
- Arduino Framework
- Leverages FreeRTOS for task management
- Uses preferences for NVS storage
- Uses SPIFFS for serving HTML
> ```Motion Sensor ```
- Wemos D1 Mini (ESP8266)
- Arduino Framework
- IR Pyroelectric Infrared PIR Motion Sensing Detector Module
> ```Climate Sensor``` 
- Wemos D1 Mini (ESP8266)
- Arduino Framework
- DHT22 Thermometer Temperature and Humidity Sensor
> ```Relay Actuator``` 
- Wemos D1 Mini (ESP8266)
- Arduino Framework
-  5V Relay 1-Channel High-active or Low-active
> ```Air Quality Sensor``` 
- Wemos D1 Mini (ESP8266)
- Arduino Framework
- BME680 Sensor Module with Level Converter - Air Pressure - Air Quality - Humidity - Temperature



***
### Device Gateway Features
- Local asset management, including json data of the asset representation in OpenRemote.
- Onboarding process for IoT devices over local UDP.
- Processing and forwarding data received from devices over UDP, attempts publish data for multiple attributes at once.
- Processing and forwarding control events from OpenRemote to the specified device over UDP.
- Acknowledging pending attribute events received from OpenRemote.
- Web interface for managing the locally onboarded assets/devices. (Available at the IP of the Gateway)
- Reconnection procedures for both MQTT and WIFI.
- Persisting asset data in NVS.
***


