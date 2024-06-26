#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <string>
#include <vector>
#include <algorithm>
#include "device_asset.h"
#include <Preferences.h>

// SUPPORTED TYPES: PlugAsset, PresenceSensorAsset, EnvironmentSensorAsset

/// @brief Device Manager class
/// This class is responsible for managing devices and their assets
/// It keeps track of devices that are pending onboarding, devices that are onboarded and their assets
/// It also stores the device assets in the ESP32's preferences (non-volatile memory, key-value store)
class AssetManager
{

public:
    std::vector<std::string> pendingOnboarding;
    std::vector<DeviceAsset> assets;
    Preferences &preferences;

    /// @brief Constructor
    /// @param clientId Client ID for MQTT
    /// @param _client Reference to a PubSubClient object
    AssetManager(Preferences &preferences) : preferences(preferences)
    {
    }

    /// @brief Initialize the device manager
    void init()
    {
        uint count = preferences.getUInt("count", 0);

        if (count == 0)
        {
            return;
        }

        for (int i = 0; i < preferences.getUInt("count", 0); i++)
        {
            std::string id = std::to_string(i);
            std::string assetJson = std::string(preferences.getString(id.c_str(), "").c_str());
            if (assetJson != "")
            {
                DeviceAsset asset = DeviceAsset::fromJson(assetJson.c_str());
                assets.push_back(std::move(asset));
            }
        }
    }

    /// @brief Set the connection details for a device
    void setConnection(std::string deviceSerial, IPAddress address, uint port)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].sn.c_str() == deviceSerial)
            {
                assets[i].address = address;
                assets[i].port = port;
                return;
            }
        }
    }

    /// @brief Add a device to the pending onboarding list
    /// @param deviceSerial
    void addPendingOnboarding(std::string deviceSerial)
    {
        pendingOnboarding.push_back(deviceSerial);
    }

    /// @brief Remove a device from the pending onboarding list
    void removePendingOnboarding(std::string deviceSerial)
    {
        pendingOnboarding.erase(std::remove(pendingOnboarding.begin(), pendingOnboarding.end(), deviceSerial), pendingOnboarding.end());
    }

    /// @brief  Check if a device is pending onboarding
    /// @param deviceSerial
    /// @return bool
    bool isOnboardingPending(std::string deviceSerial)
    {
        return std::find(pendingOnboarding.begin(), pendingOnboarding.end(), deviceSerial) != pendingOnboarding.end();
    }

    /// @brief Check if a device is onboarded with OpenRemote
    /// @param deviceSerial
    /// @return bool
    bool isDeviceOnboarded(std::string deviceSerial)
    {
        return std::any_of(assets.begin(), assets.end(), [&deviceSerial](const DeviceAsset &asset)
                           { return asset.sn == deviceSerial; });
    }

    /// @brief Add a device asset to the device manager,
    /// should only be called after confirming the device has been created on OpenRemote (MQTT Callback)
    void addDeviceAsset(DeviceAsset asset)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].id == asset.id)
            {
                return;
            }
        }
        assets.push_back(asset);

        // get the current count of assets
        uint id = preferences.getUInt("count", 0);
        // increment the count, id = count
        preferences.putString(std::to_string(id).c_str(), asset.managerJson.c_str());
        preferences.putUInt("count", assets.size());

        uint count = preferences.getUInt("count", 0);
    }

    /// @brief Handle an attribute event from the OpenRemote platform, updates the local device asset representation respectively
    /// @param attributeEvent JSON string, containing the attribute event
    void handleManagerAttributeEvent(std::string attributeEvent)
    {
        //{"eventType":"attribute","ref":{"id":"27Nz70ewisZB4CdPVX1Gp2","name":"notes"},"value":null,"timestamp":1717614718858,"deleted":false,"realm":"master"}
    }

    /// @brief Get the device asset ID by device serial number
    /// @param deviceSerial
    std::string getDeviceAssetId(std::string deviceSerial)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].sn.c_str() == deviceSerial)
            {
                return assets[i].id;
            }
        }
        return "";
    }

    bool deleteDeviceAssetById(std::string assetId)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].id.c_str() == assetId)
            {
                assets.erase(assets.begin() + i);
                updatePreferences();
                return true;
            }
        }
        return false;
    }

    /// @brief Update the preferences with the current device assets, should be called after adding, updating or removing a device asset
    void updatePreferences()
    {
        uint count = preferences.getUInt("count", 0);
        for (int i = 0; i < count; i++)
        {
            preferences.remove(std::to_string(i).c_str());
        }
        for (int i = 0; i < assets.size(); i++)
        {
            preferences.putString(std::to_string(i).c_str(), assets[i].managerJson.c_str());
        }
        preferences.putUInt("count", assets.size());
    }

    /// @brief Update the device asset JSON representation
    bool updateDeviceAssetJson(std::string assetId, std::string json)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].id.c_str() == assetId)
            {
                assets[i].managerJson = json;
                updatePreferences();
                return true;
            }
        }
        return false;
    }

    /// @brief Remove a device asset from the device manager (should only be called after confirming the device has been removed from OpenRemote)
    DeviceAsset getDeviceAsset(std::string deviceSerial)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].sn.c_str() == deviceSerial)
            {
                return assets[i];
            }
        }
        return DeviceAsset();
    }

    /// @brief Get a device asset by ID
    /// @param id
    DeviceAsset getDeviceAssetById(std::string id)
    {
        for (int i = 0; i < assets.size(); i++)
        {
            if (assets[i].id == id.c_str())
            {
                return assets[i];
            }
        }
        return DeviceAsset();
    }
};

#endif
