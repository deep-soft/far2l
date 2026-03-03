#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "../core/Types.h"
#include "../ptp/PtpSession.h"

namespace proto {

struct DeviceInfoData {
    uint32_t standard_version = 0;
    uint32_t vendor_extension_id = 0;
    uint16_t vendor_extension_version = 0;
    std::string vendor_extension_desc;
    uint16_t functional_mode = 0;
    std::unordered_set<uint16_t> operations_supported;
    std::string manufacturer;
    std::string model;
    std::string serial;
};

class MtpSession {
public:
    explicit MtpSession(PtpSession& ptp)
        : _ptp(ptp) {}

    Result<DeviceInfoData> GetDeviceInfo();
    Result<std::vector<StorageId>> GetStorageIds();
    Result<StorageEntry> GetStorageInfo(StorageId storageId);
    Result<std::vector<ObjectHandle>> GetObjectHandles(StorageId storageId, ObjectHandle parent);
    Result<ObjectEntry> GetObjectInfo(ObjectHandle handle);
    Result<std::vector<ObjectEntry>> GetObjectPropList(ObjectHandle parent, uint32_t depth = 1);

    Result<std::vector<uint8_t>> GetObject(ObjectHandle handle);
    Result<std::vector<uint8_t>> GetPartialObject(ObjectHandle handle, uint32_t offset, uint32_t max_bytes);
    Result<ObjectHandle> SendObjectInfo(const std::string& name,
                                        StorageId storageId,
                                        ObjectHandle parent,
                                        bool is_dir,
                                        uint64_t size);
    Status SendObject(ObjectHandle handle, const std::vector<uint8_t>& payload);
    Result<ObjectHandle> CreateFolder(const std::string& name, StorageId storageId, ObjectHandle parent);
    Status SetObjectFileName(ObjectHandle handle, const std::string& new_name);
    Status SetObjectFileNameViaPropList(ObjectHandle handle, const std::string& new_name);
    Result<ObjectHandle> CopyObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent);
    Status MoveObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent);

    Status DeleteObject(ObjectHandle handle, uint16_t format = 0);

    CapabilityProfile BuildCapabilityProfile(const DeviceInfoData& info) const;

private:
    static uint16_t ReadU16(const uint8_t* p);
    static uint32_t ReadU32(const uint8_t* p);
    static uint64_t ReadU64(const uint8_t* p);

    static Result<std::vector<uint16_t>> ParseU16Array(const std::vector<uint8_t>& data, size_t& off);
    static Result<std::vector<uint32_t>> ParseU32Array(const std::vector<uint8_t>& data, size_t& off);
    static Result<std::string> ParsePtpString(const std::vector<uint8_t>& data, size_t& off);
    static uint64_t ParseMtpDateTime(const std::string& s);
    static void AppendU16(std::vector<uint8_t>& out, uint16_t v);
    static void AppendU32(std::vector<uint8_t>& out, uint32_t v);
    static void AppendU64(std::vector<uint8_t>& out, uint64_t v);
    static void AppendPtpString(std::vector<uint8_t>& out, const std::string& s);
    static bool SkipDataByType(const std::vector<uint8_t>& data, size_t& off, uint16_t dataType);
    static bool ParseUnsignedByType(const std::vector<uint8_t>& data, size_t& off, uint16_t dataType, uint64_t& v);
    static bool ParseStringByType(const std::vector<uint8_t>& data, size_t& off, uint16_t dataType, std::string& out);

    PtpSession& _ptp;
};

} // namespace proto
