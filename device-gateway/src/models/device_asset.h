#include <WString.h>
#include <ArduinoJson.h>

struct DeviceAssetConnection
{
    IPAddress ip;
    int port;
};

struct DeviceAsset
{
    String id;
    String sn;
    String type;

    // used internally for udp messaging
    DeviceAssetConnection connection;

    String toJson()
    {
        DynamicJsonDocument doc(1024);
        doc["id"] = id;
        doc["type"] = type;
        doc["attributes"]["sn"]["value"] = sn;

        String output;
        serializeJson(doc, output);
        return output;
    }

    static DeviceAsset fromJson(String json)
    {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, json);
        DeviceAsset asset;
        asset.id = doc["id"].as<String>();
        asset.type = doc["type"].as<String>();
        asset.sn = doc["attributes"]["sn"]["value"].as<String>();
        return asset;
    }
};