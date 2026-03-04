#pragma once

#include <chrono>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../core/Types.h"

namespace proto {

class DirectoryCache {
public:
    struct Key {
        std::string device_key;
        StorageId storage_id = 0;
        ObjectHandle parent = 0;

        bool operator==(const Key& o) const {
            return device_key == o.device_key && storage_id == o.storage_id && parent == o.parent;
        }
    };

    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = std::hash<std::string>{}(k.device_key);
            h ^= std::hash<uint32_t>{}(k.storage_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.parent) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct Snapshot {
        std::vector<ObjectEntry> entries;
        uint64_t generation = 0;
        std::chrono::steady_clock::time_point ts{};
    };

    DirectoryCache(size_t max_dirs = 2048,
                   size_t max_entries = 200000,
                   std::chrono::seconds ttl = std::chrono::seconds(5));

    bool Get(const Key& key, Snapshot& out, bool& fresh);
    void Put(const Key& key, std::vector<ObjectEntry> entries);
    void Invalidate(const Key& key);
    void InvalidateDevice(const std::string& device_key);
    void Clear();

private:
    void Touch(const Key& key);
    void Evict();

    struct Entry {
        Snapshot snapshot;
        size_t entry_count = 0;
    };

    std::mutex _mtx;
    size_t _max_dirs;
    size_t _max_entries;
    std::chrono::seconds _ttl;
    uint64_t _generation = 0;
    size_t _total_entries = 0;
    std::unordered_map<Key, Entry, KeyHash> _map;
    std::list<Key> _lru;
};

} // namespace proto
