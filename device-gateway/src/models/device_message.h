#include <WString.h>
#include <ArduinoJson.h>

// onboarding messages
#define ONBOARD_OK "ONBOARD_OK"
#define ONBOARD_FAIL "ONBOARD_FAIL"
#define ONBOARD_REQ "ONBOARD_REQ"

// action messages
#define ACTION_ON "ACTION_ON"
#define ACTION_OFF "ACTION_OFF"

enum MessageType
{
    ONBOARD_MESSAGE,
    DATA_MESSAGE
};

struct DeviceMessage
{
    String device_name;
    String device_sn;
    String device_type;
    String data;
    MessageType message_type;

    DeviceMessage(String device_name, String device_sn, String device_type, String data, MessageType message_type)
    {
        this->device_name = device_name;
        this->device_sn = device_sn;
        this->device_type = device_type;
        this->data = data;
        this->message_type = message_type;
    }

    String toJson()
    {
        DynamicJsonDocument doc(1024);
        doc["device_name"] = device_name;
        doc["device_sn"] = device_sn;
        doc["device_type"] = device_type;
        doc["data"] = data;
        doc["message_type"] = (int)message_type;

        String output;
        serializeJson(doc, output);
        return output;
    }

    static DeviceMessage fromJson(String json)
    {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, json);
        return DeviceMessage(doc["device_name"].as<String>(), doc["device_sn"].as<String>(), doc["device_type"].as<String>(), doc["data"].as<String>(), (MessageType)doc["message_type"].as<int>());
    }
};
