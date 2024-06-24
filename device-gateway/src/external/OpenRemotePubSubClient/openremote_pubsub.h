#ifndef OPENREMOTE_PUBSUB_H
#define OPENREMOTE_PUBSUB_H

#include <PubSubClient.h>

// This class simplifies the interaction with the OpenRemote MQTT API
// functions:
// - createAsset
// - getAsset (missing)
// - deleteAsset
// - updateAsset
// - updateAttribute
// - getAttribute
// - getAttributeValue (missing)
// - acknowledgeGatewayEvent
// - subscribeToPendingGatewayEvents
// NOTE: Missing methods for subscribing to the various filter posibilities e.g. specific attribute events of an asset

class OpenRemotePubSub
{
public:
    PubSubClient &client;

    /// @brief Constructor for OpenRemotePubSub, a class that simplifies the interaction with the OpenRemote MQTT API
    /// @param clientId Client ID for MQTT (must be unique per client, in case of gateway it must use the clientId from the gateway asset)
    /// @param _client Reference to a PubSubClient object
    OpenRemotePubSub(PubSubClient &_client) : client(_client)
    {
        if (client.getBufferSize() < 16384)
        {
            client.setBufferSize(16384); // Enforce buffer size to 16KB, events can be quite large (Especially with SSL enabled)
        }
    }

    /// @brief Publish an event
    /// @param realm (realm of the asset)
    /// @param assetId (ID of the asset, 22 character string)
    /// @param eventName (name of the event)
    /// @param eventValue (value of the event)
    /// @param subscribeToResponse (default is false)
    /// @return bool (true if the message was published)
    bool updateAttribute(std::string realm, std::string clientId, std::string assetId, std::string attributeName, std::string attributeValue, bool subscribeToResponse = false)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/attributes/%s/update", realm.c_str(), clientId.c_str(), assetId.c_str(), attributeName.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "%s/response", topic);
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }

        const char *payload = attributeValue.c_str();
        return client.publish(topic, payload);
    }

    /// @brief Update multiple attributes
    /// @param realm (realm of the asset)
    /// @param assetId (ID of the asset, 22 character string)
    /// @param attributeTemplate (JSON representation of the list of attributes)
    /// @param subscribeToResponse (default is false)
    /// @return bool (true if the message was published)
    bool updateMultipleAttributes(std::string realm, std::string clientId, std::string assetId, std::string attributeTemplate, bool subscribeToResponse = false)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/attributes/update", realm.c_str(), clientId.c_str(), assetId.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "%s/response", topic);
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }

        const char *payload = attributeTemplate.c_str();
        return client.publish(topic, payload);
    }

    /// @brief Get an attribute
    /// @param realm (realm of the asset)
    /// @param assetId (ID of the asset, 22 character string)
    /// @param attributeName (name of the attribute)
    /// @param subscribeToResponse (default is false)
    /// @return bool (true if the message was published)
    bool getAttribute(std::string realm, std::string clientId, std::string assetId, std::string attributeName, bool subscribeToResponse = false)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/attributes/%s/get", realm.c_str(), clientId.c_str(), assetId.c_str(), attributeName.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "%s/response", topic);
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }
        return client.publish(topic, ""); // Requests don't require a payload
    }

    /// @brief Create an asset
    /// @param realm (realm of the asset)
    /// @param assetTemplate (JSON representation of the asset)
    /// @param responseIdentifier (can be any string, used to correlate the response with the request)
    /// @param subscribeToResponse (default is false)
    /// @return bool (true if the message was published)
    bool createAsset(std::string realm, std::string clientId, std::string assetTemplate, std::string responseIdentifier, bool subscribeToResponse = false)
    {
        Serial.println(clientId.c_str());
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/create", realm.c_str(), clientId.c_str(), responseIdentifier.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "%s/response", topic);
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }

        const char *payload = assetTemplate.c_str();
        return client.publish(topic, payload);
    }

    /// @brief delete an asset
    /// @param realm (realm of the asset)
    /// @param assetId (ID of the asset, 22 character string)
    /// @param subscribeToResponse (default is false)
    /// @return bool (true if the message was published)
    bool deleteAsset(std::string realm, std::string clientId, std::string assetId, bool subscribeToResponse = false)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/delete", realm.c_str(), clientId.c_str(), assetId.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "%s/response", topic);
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }

        return client.publish(topic, "");
    }

    /// @brief Update an asset
    /// @param realm
    /// @param assetId (ID of the asset, 22 character string)
    /// @param assetTemplate (JSON representation of the asset)
    /// @param subscribeToResponse (default is false)
    bool updateAsset(std::string realm, std::string clientId, std::string assetId, std::string assetTemplate, bool subscribeToResponse = false)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/update", realm.c_str(), clientId.c_str(), assetId.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "%s/response", topic);
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }

        const char *payload = assetTemplate.c_str();
        return client.publish(topic, payload);
    }

    /// @brief Acknowledge a gateway event (e.g. attribute change)
    /// @param realm (realm of the gateway)
    /// @param ackId (ID of the event to acknowledge)
    /// @return bool (true if the message was published)
    bool acknowledgeGatewayEvent(std::string realm, std::string clientId, std::string ackId)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/gateway/events/acknowledge", realm.c_str(), clientId.c_str());
        return client.publish(topic, ackId.c_str());
    }

    /// @brief Subscribe to pending gateway events
    /// @param realm (realm of the gateway)
    /// @return bool (true if the subscription was successful)
    bool subscribeToPendingGatewayEvents(std::string realm, std::string clientId)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/gateway/events/pending", realm.c_str(), clientId.c_str());
        return client.subscribe(topic);
    }

    bool autoProvisionDevice(std::string cert, std::string uniqueId, bool subscribeToResponse = true)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256]; // /provisioning/<uniqueId>/request
        snprintf(topic, sizeof(topic), "provisioning/%s/request", uniqueId.c_str());

        if (subscribeToResponse)
        {
            char responseTopic[256];
            snprintf(responseTopic, sizeof(responseTopic), "provisioning/%s/response", uniqueId.c_str());
            if (!client.subscribe(responseTopic))
            {
                return false;
            }
        }

        JsonDocument doc;
        doc["type"] = "x509";
        doc["cert"] = cert;

        return client.publish(topic, doc.as<String>().c_str());
    }
};

#endif // OPENREMOTE_PUBSUB_H
