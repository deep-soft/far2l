#include "MtpDeviceModel.h"

#include "../MTPLog.h"

#include <chrono>
#include <unordered_set>

namespace proto {

MtpDeviceModel::MtpDeviceModel(const std::string& device_key,
                               uint16_t vendor_id,
                               uint16_t product_id,
                               MtpSession& session,
                               DirectoryCache& dir_cache,
                               QuirkRegistry& quirks)
    : _deviceKey(device_key)
    , _vendorId(vendor_id)
    , _productId(product_id)
    , _session(session)
    , _dirCache(dir_cache)
    , _quirkRegistry(quirks) {
}

Status MtpDeviceModel::RefreshDeviceInfo() {
    auto di = _session.GetDeviceInfo();
    if (!di.ok) {
        return Status::Failure(di.code, di.message, di.retryable);
    }

    _deviceInfo = std::move(di.value);
    _caps = _session.BuildCapabilityProfile(_deviceInfo);
    _quirks = _quirkRegistry.Resolve(_vendorId,
                                     _productId,
                                     _deviceInfo.manufacturer,
                                     _deviceInfo.model,
                                     _deviceInfo.vendor_extension_id,
                                     _caps);
    _propListUsable = _caps.has_get_object_prop_list && !_quirks.disable_get_object_prop_list;
    _propListProbed = false;
    return OkStatus();
}

Result<std::vector<StorageEntry>> MtpDeviceModel::RefreshStorages() {
    auto ids = _session.GetStorageIds();
    if (!ids.ok) {
        return Result<std::vector<StorageEntry>>::Failure(ids.code, ids.message, ids.retryable);
    }

    std::vector<StorageEntry> out;
    out.reserve(ids.value.size());
    for (StorageId id : ids.value) {
        auto si = _session.GetStorageInfo(id);
        if (!si.ok) {
            continue;
        }
        _storages[id] = si.value;
        out.push_back(si.value);
    }
    return Result<std::vector<StorageEntry>>::Success(std::move(out));
}

Result<std::vector<ObjectEntry>> MtpDeviceModel::ListChildren(StorageId storageId, ObjectHandle parent) {
    const auto started = std::chrono::steady_clock::now();
    DirectoryCache::Snapshot snap;
    bool fresh = false;
    DirectoryCache::Key key{_deviceKey, storageId, parent};
    if (_dirCache.Get(key, snap, fresh)) {
        if (fresh) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            (void)ms;
            DBG("ListChildren cache-hit device=%s storage=%u parent=%u count=%zu dur_ms=%lld",
                _deviceKey.c_str(), storageId, parent, snap.entries.size(), static_cast<long long>(ms));
            return Result<std::vector<ObjectEntry>>::Success(snap.entries);
        }
    }

    std::vector<ObjectHandle> parent_candidates;
    if (parent == 0) {
        if (_quirks.broken_root_parent) {
            parent_candidates = {0xFFFFFFFFu, 0u};
        } else {
            parent_candidates = {0u, 0xFFFFFFFFu};
        }
    } else {
        parent_candidates = {parent};
    }

    ObjectHandle selected_parent = parent_candidates.front();
    size_t handles_count = 0;
    std::vector<ObjectEntry> entries;
    ListTier tier = ListTier::Tier3ObjectInfo;

    if (_propListUsable) {
        for (ObjectHandle candidate_parent : parent_candidates) {
            auto propList = _session.GetObjectPropList(candidate_parent, 1);
            if (!propList.ok) {
                continue;
            }

            std::vector<ObjectEntry> from_prop;
            from_prop.reserve(propList.value.size());
            for (auto& e : propList.value) {
                const bool parent_match =
                    (e.parent == candidate_parent) ||
                    (candidate_parent == 0xFFFFFFFFu && e.parent == 0);
                if (!parent_match) {
                    continue;
                }
                if (e.storage_id != 0 && e.storage_id != storageId) {
                    continue;
                }
                if (e.storage_id == 0) {
                    e.storage_id = storageId;
                }
                _objects[e.handle] = e;
                from_prop.push_back(std::move(e));
            }

            if (!_propListProbed) {
                auto probe_handles = _session.GetObjectHandles(storageId, candidate_parent);
                if (probe_handles.ok) {
                    handles_count = probe_handles.value.size();
                    std::unordered_set<ObjectHandle> expected(probe_handles.value.begin(), probe_handles.value.end());
                    std::unordered_set<ObjectHandle> got;
                    got.reserve(from_prop.size());
                    for (const auto& e : from_prop) {
                        if (expected.count(e.handle) != 0) {
                            got.insert(e.handle);
                        }
                    }
                    const bool complete = (got.size() == expected.size());
                    const bool mostly_complete = expected.empty() ||
                        (got.size() * 100 >= expected.size() * 70);
                    if (!complete && !mostly_complete) {
                        _propListUsable = false;
                        _quirkRegistry.MarkPropListBroken(_vendorId,
                                                          _productId,
                                                          _deviceInfo.manufacturer,
                                                          _deviceInfo.model,
                                                          _deviceInfo.vendor_extension_id);
                    } else if (from_prop.size() < expected.size()) {
                        tier = ListTier::Tier2Hybrid;
                        std::unordered_set<ObjectHandle> have;
                        have.reserve(from_prop.size());
                        for (const auto& e : from_prop) {
                            have.insert(e.handle);
                        }
                        for (ObjectHandle h : probe_handles.value) {
                            if (have.count(h) != 0) {
                                continue;
                            }
                            auto oi = _session.GetObjectInfo(h);
                            if (!oi.ok) {
                                continue;
                            }
                            _objects[h] = oi.value;
                            from_prop.push_back(oi.value);
                        }
                    }
                }
                _propListProbed = true;
            }

            if (_propListUsable) {
                selected_parent = candidate_parent;
                entries = std::move(from_prop);
                if (tier != ListTier::Tier2Hybrid) {
                    tier = ListTier::Tier1PropList;
                }
                break;
            }
        }

        if (entries.empty() && !_propListUsable) {
            _propListProbed = true;
        }
    }

    if (entries.empty()) {
        Result<std::vector<ObjectHandle>> handles =
            Result<std::vector<ObjectHandle>>::Failure(ErrorCode::Io, "not started");
        for (ObjectHandle candidate_parent : parent_candidates) {
            handles = _session.GetObjectHandles(storageId, candidate_parent);
            if (handles.ok) {
                selected_parent = candidate_parent;
                break;
            }
        }
        if (!handles.ok) {
            return Result<std::vector<ObjectEntry>>::Failure(handles.code, handles.message, handles.retryable);
        }

        handles_count = handles.value.size();
        entries.reserve(handles.value.size());
        for (ObjectHandle h : handles.value) {
            auto oi = _session.GetObjectInfo(h);
            if (!oi.ok) {
                continue;
            }
            _objects[h] = oi.value;
            entries.push_back(oi.value);
        }
        tier = ListTier::Tier3ObjectInfo;
    }

    if (tier == ListTier::Tier1PropList) {
        ++_tier1_hits;
    } else if (tier == ListTier::Tier2Hybrid) {
        ++_tier2_hits;
    } else {
        ++_tier3_hits;
    }

    _dirCache.Put(key, entries);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    (void)ms;
    DBG("ListChildren cache-miss device=%s storage=%u parent=%u selected_parent=%u handles=%zu entries=%zu tier=%d t1=%llu t2=%llu t3=%llu prop_list=%d dur_ms=%lld",
        _deviceKey.c_str(),
        storageId,
        parent,
        selected_parent,
        handles_count,
        entries.size(),
        static_cast<int>(tier),
        static_cast<unsigned long long>(_tier1_hits),
        static_cast<unsigned long long>(_tier2_hits),
        static_cast<unsigned long long>(_tier3_hits),
        _propListUsable ? 1 : 0,
        static_cast<long long>(ms));
    return Result<std::vector<ObjectEntry>>::Success(std::move(entries));
}

Result<ObjectEntry> MtpDeviceModel::Stat(ObjectHandle handle) {
    auto it = _objects.find(handle);
    if (it != _objects.end()) {
        return Result<ObjectEntry>::Success(it->second);
    }

    auto oi = _session.GetObjectInfo(handle);
    if (!oi.ok) {
        return Result<ObjectEntry>::Failure(oi.code, oi.message, oi.retryable);
    }
    _objects[handle] = oi.value;
    return oi;
}

Result<ObjectEntry> MtpDeviceModel::FindChildByName(StorageId storageId, ObjectHandle parent, const std::string& name) {
    auto children = ListChildren(storageId, parent);
    if (!children.ok) {
        return Result<ObjectEntry>::Failure(children.code, children.message, children.retryable);
    }
    for (const auto& e : children.value) {
        if (e.name == name) {
            return Result<ObjectEntry>::Success(e);
        }
    }
    return Result<ObjectEntry>::Failure(ErrorCode::NotFound, "Child object not found");
}

void MtpDeviceModel::InvalidateOnMutation(StorageId storageId, ObjectHandle parent) {
    InvalidateAncestors(storageId, parent);
}

void MtpDeviceModel::InvalidateAncestors(StorageId storageId, ObjectHandle parent) {
    ObjectHandle cur = parent;
    for (size_t depth = 0; depth < 1024; ++depth) {
        _dirCache.Invalidate(DirectoryCache::Key{_deviceKey, storageId, cur});
        if (cur == 0) {
            break;
        }
        auto it = _objects.find(cur);
        if (it == _objects.end()) {
            break;
        }
        if (it->second.parent == cur) {
            break;
        }
        cur = it->second.parent;
    }
}

} // namespace proto
