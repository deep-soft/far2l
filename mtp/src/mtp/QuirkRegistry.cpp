#include "QuirkRegistry.h"

namespace proto {

QuirkFlags QuirkRegistry::Resolve(uint16_t vid,
                                  uint16_t pid,
                                  const std::string& manufacturer,
                                  const std::string& model,
                                  uint32_t vendor_ext_id,
                                  const CapabilityProfile& caps) const {
    QuirkFlags q;
    if (!caps.has_get_object_prop_list) {
        q.disable_get_object_prop_list = true;
    }
    if (!caps.has_get_partial_object) {
        q.no_partial_object = true;
    }

    // Conservative defaults for Apple/iOS PTP stack.
    if (vid == 0x05ac) {
        q.disable_get_object_prop_list = true;
        q.no_partial_object = true;
        q.timestamp_unreliable = true;
    }

    {
        std::lock_guard<std::mutex> lk(_mtx);
        if (_prop_list_broken_runtime.count(RuntimeKey(vid, pid, manufacturer, model, vendor_ext_id)) != 0) {
            q.disable_get_object_prop_list = true;
        }
    }

    return q;
}

void QuirkRegistry::MarkPropListBroken(uint16_t vid,
                                       uint16_t pid,
                                       const std::string& manufacturer,
                                       const std::string& model,
                                       uint32_t vendor_ext_id) {
    std::lock_guard<std::mutex> lk(_mtx);
    _prop_list_broken_runtime.insert(RuntimeKey(vid, pid, manufacturer, model, vendor_ext_id));
}

std::string QuirkRegistry::RuntimeKey(uint16_t vid,
                                      uint16_t pid,
                                      const std::string& manufacturer,
                                      const std::string& model,
                                      uint32_t vendor_ext_id) const {
    return std::to_string(vid) + ":" +
           std::to_string(pid) + ":" +
           std::to_string(vendor_ext_id) + ":" +
           manufacturer + ":" +
           model;
}

} // namespace proto
