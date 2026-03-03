#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "../core/Types.h"

namespace proto {

class QuirkRegistry {
public:
    QuirkFlags Resolve(uint16_t vid,
                       uint16_t pid,
                       const std::string& manufacturer,
                       const std::string& model,
                       uint32_t vendor_ext_id,
                       const CapabilityProfile& caps) const;

    void MarkPropListBroken(uint16_t vid,
                            uint16_t pid,
                            const std::string& manufacturer,
                            const std::string& model,
                            uint32_t vendor_ext_id);

private:
    std::string RuntimeKey(uint16_t vid,
                           uint16_t pid,
                           const std::string& manufacturer,
                           const std::string& model,
                           uint32_t vendor_ext_id) const;

    mutable std::mutex _mtx;
    std::unordered_set<std::string> _prop_list_broken_runtime;
};

} // namespace proto
