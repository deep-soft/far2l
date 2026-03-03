#include "DirectoryCache.h"

namespace proto {

DirectoryCache::DirectoryCache(size_t max_dirs, size_t max_entries, std::chrono::seconds ttl)
    : _max_dirs(max_dirs)
    , _max_entries(max_entries)
    , _ttl(ttl) {
}

bool DirectoryCache::Get(const Key& key, Snapshot& out, bool& fresh) {
    std::lock_guard<std::mutex> lk(_mtx);
    auto it = _map.find(key);
    if (it == _map.end()) {
        return false;
    }
    out = it->second.snapshot;
    fresh = (std::chrono::steady_clock::now() - out.ts) <= _ttl;
    Touch(key);
    return true;
}

void DirectoryCache::Put(const Key& key, std::vector<ObjectEntry> entries) {
    std::lock_guard<std::mutex> lk(_mtx);

    auto it = _map.find(key);
    if (it != _map.end()) {
        _total_entries -= it->second.entry_count;
    }

    Entry e;
    e.entry_count = entries.size();
    e.snapshot.entries = std::move(entries);
    e.snapshot.generation = ++_generation;
    e.snapshot.ts = std::chrono::steady_clock::now();

    _total_entries += e.entry_count;
    _map[key] = std::move(e);
    Touch(key);
    Evict();
}

void DirectoryCache::Invalidate(const Key& key) {
    std::lock_guard<std::mutex> lk(_mtx);
    auto it = _map.find(key);
    if (it != _map.end()) {
        _total_entries -= it->second.entry_count;
        _map.erase(it);
    }
    _lru.remove_if([&](const Key& k) { return k == key; });
}

void DirectoryCache::InvalidateDevice(const std::string& device_key) {
    std::lock_guard<std::mutex> lk(_mtx);
    for (auto it = _map.begin(); it != _map.end();) {
        if (it->first.device_key == device_key) {
            _total_entries -= it->second.entry_count;
            it = _map.erase(it);
        } else {
            ++it;
        }
    }
    _lru.remove_if([&](const Key& k) { return k.device_key == device_key; });
}

void DirectoryCache::Clear() {
    std::lock_guard<std::mutex> lk(_mtx);
    _map.clear();
    _lru.clear();
    _total_entries = 0;
}

void DirectoryCache::Touch(const Key& key) {
    _lru.remove_if([&](const Key& k) { return k == key; });
    _lru.push_front(key);
}

void DirectoryCache::Evict() {
    while ((_map.size() > _max_dirs || _total_entries > _max_entries) && !_lru.empty()) {
        Key old = _lru.back();
        _lru.pop_back();
        auto it = _map.find(old);
        if (it != _map.end()) {
            _total_entries -= it->second.entry_count;
            _map.erase(it);
        }
    }
}

} // namespace proto
