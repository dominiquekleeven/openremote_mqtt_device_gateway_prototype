
#include <ArduinoJson.h>

#define PLUG_ASSET "PlugAsset"
#define PRESENCE_SENSOR_ASSET "PresenceSensorAsset"
#define ENVIRONMENT_SENSOR_ASSET "EnvironmentSensorAsset"

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
    std::string toJson(std::vector<std::string> extras)
    {
        DynamicJsonDocument doc(8096);
        doc["type"] = type;
        doc["name"] = name;

        JsonObject attributes = doc.createNestedObject("attributes");
        if (extras.size() > 0)
        {
            for (int i = 0; i < extras.size(); i++)
            {
                JsonObject extra = attributes.createNestedObject(extras[i]);
            }
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
        std::vector<std::string> extras = {"onOff"};
        return BaseAsset::toJson(extras);
    }
};

struct PresenceSensorAsset : BaseAsset
{
    PresenceSensorAsset(std::string name, std::string sn, std::string type) : BaseAsset(name, sn, type)
    {
    }

    std::string toJson()
    {
        std::vector<std::string> extras = {"presence"};
        return BaseAsset::toJson(extras);
    }
};

struct EnvironmentSensorAsset : BaseAsset
{
    EnvironmentSensorAsset(std::string name, std::string sn, std::string type) : BaseAsset(name, sn, type)
    {
    }

    std::string toJson()
    {
        std::vector<std::string> extras = {"temperature", "relativeHumidity", "NO2Level", "ozoneLevel", "particlesPM1", "particlesPM10", "particlesPM2_5"};
        return BaseAsset::toJson(extras);
    }
};