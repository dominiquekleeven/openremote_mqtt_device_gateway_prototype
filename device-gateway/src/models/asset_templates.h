
#include <ArduinoJson.h>
#include <WString.h>

#define PLUG_ASSET "PlugAsset"
#define PRESENCE_SENSOR_ASSET "PresenceSensorAsset"

struct BaseAsset
{
    String type;
    String name;
    String sn;

    // constructor
    BaseAsset(String name, String sn, String type)
    {
        this->type = type;
        this->sn = sn;
        this->name = name;
    }

    // accept array of strings
    String toJson(String extra)
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

        String output;
        serializeJson(doc, output);
        return output;
    }
};

struct PlugAsset : BaseAsset
{
    PlugAsset(String name, String sn, String type) : BaseAsset(name, sn, type)
    {
    }

    String toJson()
    {
        return BaseAsset::toJson("onOff");
    }
};

struct PresenceSensorAsset : BaseAsset
{
    PresenceSensorAsset(String name, String sn, String type) : BaseAsset(name, sn, type)
    {
    }

    String toJson()
    {

        return BaseAsset::toJson("presence");
    }
};
