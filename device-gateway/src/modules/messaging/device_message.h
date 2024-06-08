
#include <ArduinoJson.h>

// onboarding messages
#define ONBOARD_OK "ONBOARD_OK"
#define ONBOARD_FAIL "ONBOARD_FAIL"
#define ONBOARD_REQ "ONBOARD_REQ"

// action messages - these are sent to devices to control them
#define ACTION_ON "ACTION_ON"
#define ACTION_OFF "ACTION_OFF"

// message types
// ONBOARD_MESSAGE: message sent to a device to onboard it
// DATA_MESSAGE: received data from a device
// ALIVE_MESSAGE: received alive message from a device, ping message
enum MessageType
{
    ONBOARD_MESSAGE,
    DATA_MESSAGE,
    ALIVE_MESSAGE
};

// Generic messaging structure for device based communication
// device_name: name of the device
// device_sn: serial number of the device
// device_type: type of the device
// data: data to be sent, can be any string data
struct DeviceMessage
{
    std::string device_name;
    std::string device_sn;
    std::string device_type;
    std::string data;
    MessageType message_type;

    DeviceMessage(std::string device_name, std::string device_sn, std::string device_type, std::string data, MessageType message_type)
    {
        this->device_name = device_name;
        this->device_sn = device_sn;
        this->device_type = device_type;
        this->data = data;
        this->message_type = message_type;
    }

    std::string toJson()
    {
        DynamicJsonDocument doc(1024);
        doc["device_name"] = device_name;
        doc["device_sn"] = device_sn;
        doc["device_type"] = device_type;
        doc["data"] = data;
        doc["message_type"] = (int)message_type;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    static DeviceMessage fromJson(std::string json)
    {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, json);
        return DeviceMessage(doc["device_name"].as<std::string>(), doc["device_sn"].as<std::string>(), doc["device_type"].as<std::string>(), doc["data"].as<std::string>(), (MessageType)doc["message_type"].as<int>());
    }
};
