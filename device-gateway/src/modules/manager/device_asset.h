#include <ArduinoJson.h>

struct DeviceAsset
{
    std::string id;
    std::string sn;
    std::string type;

    // openremote manager representation (we are the source of truth for this data, so we store the json representation here)
    std::string managerJson;

    // optional ones
    IPAddress address = IPAddress();
    uint port = 0;

    std::string toString()
    {
        return "id: " + id + ", sn: " + sn + ", type: " + type;
    }

    static DeviceAsset fromJson(std::string json)
    {
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, json);
        DeviceAsset asset;
        asset.id = doc["id"].as<std::string>();
        asset.type = doc["type"].as<std::string>();
        asset.sn = doc["attributes"]["sn"]["value"].as<std::string>();
        asset.managerJson = json;
        return asset;
    }
};