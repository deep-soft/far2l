#pragma once

#include <unordered_map>
#include <vector>

#include "../core/Types.h"
#include "DirectoryCache.h"
#include "MtpSession.h"
#include "QuirkRegistry.h"

namespace proto {

class MtpDeviceModel {
public:
    MtpDeviceModel(const std::string& device_key,
                   uint16_t vendor_id,
                   uint16_t product_id,
                   MtpSession& session,
                   DirectoryCache& dir_cache,
                   QuirkRegistry& quirks);

    Status RefreshDeviceInfo();
    Result<std::vector<StorageEntry>> RefreshStorages();
    Result<std::vector<ObjectEntry>> ListChildren(StorageId storageId, ObjectHandle parent);
    Result<ObjectEntry> Stat(ObjectHandle handle);
    Result<ObjectEntry> FindChildByName(StorageId storageId, ObjectHandle parent, const std::string& name);

    void InvalidateOnMutation(StorageId storageId, ObjectHandle parent);

    const DeviceInfoData& DeviceInfo() const { return _deviceInfo; }
    const CapabilityProfile& Capabilities() const { return _caps; }
    const QuirkFlags& Quirks() const { return _quirks; }

private:
    enum class ListTier {
        Cache = 0,
        Tier1PropList = 1,
        Tier2Hybrid = 2,
        Tier3ObjectInfo = 3,
    };

    void InvalidateAncestors(StorageId storageId, ObjectHandle parent);

    std::string _deviceKey;
    uint16_t _vendorId = 0;
    uint16_t _productId = 0;
    MtpSession& _session;
    DirectoryCache& _dirCache;
    QuirkRegistry& _quirkRegistry;

    DeviceInfoData _deviceInfo;
    CapabilityProfile _caps;
    QuirkFlags _quirks;
    bool _propListUsable = false;
    bool _propListProbed = false;
    uint64_t _tier1_hits = 0;
    uint64_t _tier2_hits = 0;
    uint64_t _tier3_hits = 0;
    std::unordered_map<StorageId, StorageEntry> _storages;
    std::unordered_map<ObjectHandle, ObjectEntry> _objects;
};

} // namespace proto
