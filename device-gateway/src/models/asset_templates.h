
#include <ArduinoJson.h>

#define PLUG_ASSET "PlugAsset"
#define PRESENCE_SENSOR_ASSET "PresenceSensorAsset"

struct BaseAsset
{
    std::string type;
    std::string name;
    std::string sn;

    // constructor
    BaseAsset(std::string name, std::string sn, std::string type)
    {
        this->type = type;
        this->sn = sn;
        this->name = name;
    }

    // accept array of std::strings
    std::string toJson(std::string extra)
    {
        DynamicJsonDocument doc(1024);
        doc["type"] = type;
        doc["name"] = name;

        JsonObject attributes = doc.createNestedObject("attributes");

        if (extra != "")
        {
            attributes.createNestedObject(extra);
        }

        JsonObject notes = attributes.createNestedObject("notes");
        JsonObject location = attributes.createNestedObject("location");
        JsonObject serial = attributes.createNestedObject("sn");
        JsonObject serialMeta = serial.createNestedObject("meta");

        serialMeta["readOnly"] = true;
        serial["name"] = "sn";
        serial["value"] = sn;
        serial["type"] = "text";

        std::string output;
        serializeJson(doc, output);
        return output;
    }
};

struct PlugAsset : BaseAsset
{
    PlugAsset(std::string name, std::string sn, std::string type) : BaseAsset(name, sn, type)
    {
    }

    std::string toJson()
    {
        return BaseAsset::toJson("onOff");
    }
};

struct PresenceSensorAsset : BaseAsset
{
    PresenceSensorAsset(std::string name, std::string sn, std::string type) : BaseAsset(name, sn, type)
    {
    }

    std::string toJson()
    {

        return BaseAsset::toJson("presence");
    }
};
