#include <ArduinoJson.h>

struct DeviceAsset
{
    std::string id;
    std::string sn;
    std::string type;

    // optional ones
    IPAddress address = IPAddress();
    uint port = 0;

    std::string toJson()
    {
        DynamicJsonDocument doc(1024);
        doc["id"] = id;
        doc["type"] = type;
        doc["attributes"]["sn"]["value"] = sn;

        std::string output;
        serializeJson(doc, output);
        return output;
    }

    std::string toString()
    {
        return "id: " + id + ", sn: " + sn + ", type: " + type;
    }

    static DeviceAsset fromJson(std::string json)
    {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, json);
        DeviceAsset asset;
        asset.id = doc["id"].as<std::string>();
        asset.type = doc["type"].as<std::string>();
        asset.sn = doc["attributes"]["sn"]["value"].as<std::string>();
        return asset;
    }
};