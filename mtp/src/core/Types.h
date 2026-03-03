#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proto {

enum class BackendKind {
    Unknown,
    MTP,
    PTP,
};

enum class ErrorCode {
    Ok,
    Busy,
    LockedOrUnauthorized,
    Disconnected,
    Timeout,
    ProtocolDesync,
    Unsupported,
    Io,
    Cancelled,
    NotFound,
    AccessDenied,
    InvalidArgument,
};

template <typename T>
struct Result {
    bool ok = false;
    T value{};
    ErrorCode code = ErrorCode::Io;
    std::string message;
    bool retryable = false;

    static Result<T> Success(T v) {
        Result<T> r;
        r.ok = true;
        r.code = ErrorCode::Ok;
        r.value = std::move(v);
        return r;
    }

    static Result<T> Failure(ErrorCode c, std::string msg, bool canRetry = false) {
        Result<T> r;
        r.ok = false;
        r.code = c;
        r.message = std::move(msg);
        r.retryable = canRetry;
        return r;
    }
};

using Status = Result<bool>;

inline Status OkStatus() {
    return Status::Success(true);
}

struct DeviceId {
    int bus = -1;
    int address = -1;
    int interface_number = -1;

    bool IsValid() const {
        return bus >= 0 && address >= 0 && interface_number >= 0;
    }

    std::string Key() const {
        return std::to_string(bus) + ":" + std::to_string(address) + ":" + std::to_string(interface_number);
    }
};

using StorageId = uint32_t;
using ObjectHandle = uint32_t;

struct DeviceEntry {
    DeviceId id;
    std::string key;
    std::string serial;
    std::string manufacturer;
    std::string product;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    BackendKind backend = BackendKind::Unknown;
};

struct StorageEntry {
    StorageId id = 0;
    std::string description;
    std::string volume;
    uint64_t free_bytes = 0;
    uint64_t max_capacity = 0;
};

struct ObjectEntry {
    ObjectHandle handle = 0;
    StorageId storage_id = 0;
    ObjectHandle parent = 0;
    std::string name;
    bool is_dir = false;
    bool is_hidden = false;
    bool is_readonly = false;
    uint64_t size = 0;
    uint64_t mtime_epoch = 0;
    uint64_t ctime_epoch = 0;
    uint16_t format = 0;
    uint16_t protection = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    std::string summary;
};

struct CapabilityProfile {
    bool has_get_object_prop_list = false;
    bool has_set_object_prop_list = false;
    bool has_get_partial_object = false;
};

struct QuirkFlags {
    bool disable_get_object_prop_list = false;
    bool broken_root_parent = false;
    bool timestamp_unreliable = false;
    bool require_small_bulk_in_chunks = false;
    bool no_partial_object = false;
};

} // namespace proto
