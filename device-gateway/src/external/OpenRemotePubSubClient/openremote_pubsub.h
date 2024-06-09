#ifndef OPENREMOTE_PUBSUB_H
#define OPENREMOTE_PUBSUB_H

#include <PubSubClient.h>

class OpenRemotePubSub
{
public:
    PubSubClient &client;
    std::string clientId;

    /// @brief Constructor
    /// @param clientId Client ID for MQTT
    /// @param _client Reference to a PubSubClient object
    OpenRemotePubSub(std::string clientId, PubSubClient &_client) : clientId(clientId), client(_client)
    {
        if (client.getBufferSize() < 16384)
        {
            client.setBufferSize(16384); // Increase buffer size to 16KB, especially important because of SSL/CA cert
        }
    }

    /// @brief Publish an event
    /// @param realm
    /// @param assetId (ID of the asset, 22 character string)
    /// @param eventName (name of the event)
    /// @param eventValue (value of the event)
    /// @param subscribeToResponse (default is false)
    /// @return bool
    bool updateAttribute(std::string realm, std::string assetId, std::string attributeName, std::string attributeValue, bool subscribeToResponse = false)
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

    /// @brief Get an attribute
    /// @param realm
    /// @param assetId (ID of the asset, 22 character string)
    /// @param attributeName (name of the attribute)
    /// @param subscribeToResponse (default is false)
    /// @return
    bool getAttribute(std::string realm, std::string assetId, std::string attributeName, bool subscribeToResponse = false)
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
    /// @param realm
    /// @param assetTemplate (JSON representation of the asset)
    /// @param responseIdentifier (can be any string, used to correlate the response with the request)
    /// @param subscribeToResponse (default is false)
    /// @return bool
    bool createAsset(std::string realm, std::string assetTemplate, std::string responseIdentifier, bool subscribeToResponse = false)
    {
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
    /// @param realm
    /// @param assetId (ID of the asset, 22 character string)
    /// @param subscribeToResponse (default is false)
    /// @return bool
    bool deleteAsset(std::string realm, std::string assetId, bool subscribeToResponse = false)
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
    bool updateAsset(std::string realm, std::string assetId, std::string assetTemplate, bool subscribeToResponse = false)
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

    /// @brief Acknowledge a gateway event
    /// @param topic
    /// @return bool
    bool acknowledgeGatewayEvent(std::string topic)
    {
        if (!client.connected())
        {
            return false;
        }
        char newTopic[256];
        snprintf(newTopic, sizeof(newTopic), "%s/ack", topic.c_str());
        Serial.println(newTopic);
        return client.publish(newTopic, "");
    }

    /// @brief Subscribe to pending gateway events
    /// @param realm
    /// @return bool
    bool subscribeToPendingGatewayEvents(std::string realm)
    {

        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/gateway/events/pending/+", realm.c_str(), clientId.c_str());
        return client.subscribe(topic);
    }
};

#endif // OPENREMOTE_PUBSUB_H
