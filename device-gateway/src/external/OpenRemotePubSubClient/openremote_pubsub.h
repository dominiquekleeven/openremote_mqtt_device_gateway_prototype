#ifndef OPENREMOTE_PUBSUB_H
#define OPENREMOTE_PUBSUB_H

#include <PubSubClient.h>

/// @brief OpenRemote MQTT Subscription result
struct SubscriptionResult
{
    char topic[256]; ///< Topic of the subscription
    bool success;    ///< Success status of the subscription
};

class OpenRemotePubSub
{
public:
    PubSubClient &client;
    String clientId;

    /// @brief Constructor
    /// @param clientId Client ID for MQTT
    /// @param _client Reference to a PubSubClient object
    OpenRemotePubSub(String clientId, PubSubClient &_client) : clientId(clientId), client(_client)
    {
        if (client.getBufferSize() < 8192)
        {
            client.setBufferSize(8192);
        }
    }

    // Attribute operations
    bool updateAttribute(String realm, String assetId, String attributeName, String attributeValue, bool subscribeToResponse = false)
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

    bool getAttribute(String realm, String assetId, String attributeName, bool subscribeToResponse = false)
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

    // Asset operations
    bool createAsset(String realm, String assetTemplate, String responseIdentifier, bool subscribeToResponse = false)
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

    bool getAsset(String realm, String assetId, bool subscribeToResponse = false)
    {
        if (!client.connected())
        {
            return false;
        }
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/%s/operations/assets/%s/get", realm.c_str(), clientId.c_str(), assetId.c_str());

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

    /// @brief Subscribe to a specific asset attribute on OpenRemote
    /// @param realm
    /// @param assetId
    /// @param attributeName
    /// @return SubscriptionResult
    SubscriptionResult subscribeToAssetAttribute(String realm, String assetId, String attributeName)
    {
        SubscriptionResult result;

        if (!client.connected())
        {
            result.success = false;
            return result;
        }

        snprintf(result.topic, sizeof(result.topic), "%s/%s/events/assets/%s/attributes/%s", realm.c_str(), clientId.c_str(), assetId.c_str(), attributeName.c_str());
        client.subscribe(result.topic) ? result.success = true : result.success = false;
        return result;
    }
};

#endif // OPENREMOTE_PUBSUB_H
