#include "MtpSession.h"

#include <algorithm>
#include <ctime>
#include <cstring>
#include <unordered_map>

#include "../ptp/PtpContainers.h"

namespace proto {

Result<DeviceInfoData> MtpSession::GetDeviceInfo() {
    auto data = _ptp.GetData(ptp::kOpGetDeviceInfo, {});
    if (!data.ok) {
        return Result<DeviceInfoData>::Failure(data.code, data.message, data.retryable);
    }

    DeviceInfoData out;
    size_t off = 0;
    if (data.value.size() < 8) {
        return Result<DeviceInfoData>::Failure(ErrorCode::ProtocolDesync, "Short device info dataset");
    }

    out.standard_version = ReadU16(data.value.data() + off);
    off += 2;
    out.vendor_extension_id = ReadU32(data.value.data() + off);
    off += 4;
    out.vendor_extension_version = ReadU16(data.value.data() + off);
    off += 2;

    auto vendDesc = ParsePtpString(data.value, off);
    if (!vendDesc.ok) {
        return Result<DeviceInfoData>::Failure(vendDesc.code, vendDesc.message);
    }
    out.vendor_extension_desc = std::move(vendDesc.value);

    if (off + 2 > data.value.size()) {
        return Result<DeviceInfoData>::Failure(ErrorCode::ProtocolDesync, "Malformed device info functional mode");
    }
    out.functional_mode = ReadU16(data.value.data() + off);
    off += 2;

    auto ops = ParseU16Array(data.value, off);
    if (!ops.ok) {
        return Result<DeviceInfoData>::Failure(ops.code, ops.message);
    }
    for (uint16_t op : ops.value) {
        out.operations_supported.insert(op);
    }

    // skip event formats, device props, capture formats, image formats
    for (int i = 0; i < 4; ++i) {
        auto arr = ParseU16Array(data.value, off);
        if (!arr.ok) {
            return Result<DeviceInfoData>::Failure(arr.code, arr.message);
        }
    }

    auto mfg = ParsePtpString(data.value, off);
    auto model = ParsePtpString(data.value, off);
    auto deviceVer = ParsePtpString(data.value, off);
    auto serial = ParsePtpString(data.value, off);
    if (!mfg.ok || !model.ok || !deviceVer.ok || !serial.ok) {
        return Result<DeviceInfoData>::Failure(ErrorCode::ProtocolDesync, "Malformed string fields in device info");
    }

    out.manufacturer = std::move(mfg.value);
    out.model = std::move(model.value);
    out.serial = std::move(serial.value);

    return Result<DeviceInfoData>::Success(std::move(out));
}

Result<std::vector<StorageId>> MtpSession::GetStorageIds() {
    auto data = _ptp.GetData(ptp::kOpGetStorageIDs, {});
    if (!data.ok) {
        return Result<std::vector<StorageId>>::Failure(data.code, data.message, data.retryable);
    }

    size_t off = 0;
    auto ids = ParseU32Array(data.value, off);
    if (!ids.ok) {
        return Result<std::vector<StorageId>>::Failure(ids.code, ids.message);
    }
    return Result<std::vector<StorageId>>::Success(std::move(ids.value));
}

Result<StorageEntry> MtpSession::GetStorageInfo(StorageId storageId) {
    auto data = _ptp.GetData(ptp::kOpGetStorageInfo, {storageId});
    if (!data.ok) {
        return Result<StorageEntry>::Failure(data.code, data.message, data.retryable);
    }

    StorageEntry s;
    s.id = storageId;

    size_t off = 0;
    if (data.value.size() < 26) {
        return Result<StorageEntry>::Failure(ErrorCode::ProtocolDesync, "Short storage info dataset");
    }

    off += 2; // storage type
    off += 2; // filesystem type
    off += 2; // access capability
    s.max_capacity = ReadU64(data.value.data() + off);
    off += 8;
    s.free_bytes = ReadU64(data.value.data() + off);
    off += 8;
    off += 4; // free objects

    auto d = ParsePtpString(data.value, off);
    auto v = ParsePtpString(data.value, off);
    if (!d.ok || !v.ok) {
        return Result<StorageEntry>::Failure(ErrorCode::ProtocolDesync, "Malformed storage info strings");
    }

    s.description = std::move(d.value);
    s.volume = std::move(v.value);
    return Result<StorageEntry>::Success(std::move(s));
}

Result<std::vector<ObjectHandle>> MtpSession::GetObjectHandles(StorageId storageId, ObjectHandle parent) {
    auto data = _ptp.GetData(ptp::kOpGetObjectHandles, {storageId, 0, parent});
    if (!data.ok) {
        return Result<std::vector<ObjectHandle>>::Failure(data.code, data.message, data.retryable);
    }

    size_t off = 0;
    auto handles = ParseU32Array(data.value, off);
    if (!handles.ok) {
        return Result<std::vector<ObjectHandle>>::Failure(handles.code, handles.message);
    }
    return Result<std::vector<ObjectHandle>>::Success(std::move(handles.value));
}

Result<ObjectEntry> MtpSession::GetObjectInfo(ObjectHandle handle) {
    auto data = _ptp.GetData(ptp::kOpGetObjectInfo, {handle});
    if (!data.ok) {
        return Result<ObjectEntry>::Failure(data.code, data.message, data.retryable);
    }

    if (data.value.size() < 52) {
        return Result<ObjectEntry>::Failure(ErrorCode::ProtocolDesync, "Short object info dataset");
    }

    ObjectEntry e;
    e.handle = handle;

    e.storage_id = ReadU32(data.value.data() + 0);
    e.format = ReadU16(data.value.data() + 4);
    e.protection = ReadU16(data.value.data() + 6);
    e.size = ReadU32(data.value.data() + 8);
    e.image_width = ReadU32(data.value.data() + 26);
    e.image_height = ReadU32(data.value.data() + 30);
    e.parent = ReadU32(data.value.data() + 38);
    e.is_dir = (e.format == 0x3001);
    e.is_readonly = (e.protection != 0);

    size_t off = 52;
    auto name = ParsePtpString(data.value, off);
    if (!name.ok) {
        return Result<ObjectEntry>::Failure(name.code, name.message);
    }
    e.name = std::move(name.value);

    auto capture = ParsePtpString(data.value, off);
    auto modified = ParsePtpString(data.value, off);
    if (capture.ok && !capture.value.empty()) {
        e.ctime_epoch = ParseMtpDateTime(capture.value);
    }
    if (modified.ok && !modified.value.empty()) {
        e.mtime_epoch = ParseMtpDateTime(modified.value);
    } else {
        e.mtime_epoch = e.ctime_epoch;
    }
    if (!e.name.empty() && e.name[0] == '.') {
        e.is_hidden = true;
    }

    return Result<ObjectEntry>::Success(std::move(e));
}

Result<std::vector<ObjectEntry>> MtpSession::GetObjectPropList(ObjectHandle parent, uint32_t depth) {
    auto data = _ptp.GetData(ptp::kOpGetObjectPropList,
                             {parent, 0x00000000u, 0x00000000u, 0x00000000u, depth});
    if (!data.ok) {
        return Result<std::vector<ObjectEntry>>::Failure(data.code, data.message, data.retryable);
    }

    size_t off = 0;
    if (off + 4 > data.value.size()) {
        return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ObjectPropList count missing");
    }
    uint32_t count = ReadU32(data.value.data() + off);
    off += 4;

    std::unordered_map<ObjectHandle, ObjectEntry> byHandle;
    byHandle.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        if (off + 8 > data.value.size()) {
            return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ObjectPropList entry header missing");
        }

        ObjectHandle handle = ReadU32(data.value.data() + off);
        off += 4;
        uint16_t propCode = ReadU16(data.value.data() + off);
        off += 2;
        uint16_t dataType = ReadU16(data.value.data() + off);
        off += 2;

        auto& e = byHandle[handle];
        e.handle = handle;

        if (propCode == 0xDC07) { // ObjectFileName
            std::string s;
            if (!ParseStringByType(data.value, off, dataType, s)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ObjectFileName parse failed");
            }
            e.name = std::move(s);
            continue;
        }

        if (propCode == 0xDC01) { // StorageID
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "StorageID parse failed");
            }
            e.storage_id = static_cast<StorageId>(v);
            continue;
        }

        if (propCode == 0xDC0B) { // ParentObject
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ParentObject parse failed");
            }
            e.parent = static_cast<ObjectHandle>(v);
            continue;
        }

        if (propCode == 0xDC02) { // ObjectFormat
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ObjectFormat parse failed");
            }
            e.format = static_cast<uint16_t>(v);
            e.is_dir = (e.format == 0x3001);
            continue;
        }

        if (propCode == 0xDC03) { // ProtectionStatus
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ProtectionStatus parse failed");
            }
            e.protection = static_cast<uint16_t>(v);
            e.is_readonly = (e.protection != 0);
            continue;
        }

        if (propCode == 0xDC04) { // ObjectSize
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ObjectSize parse failed");
            }
            e.size = v;
            continue;
        }

        if (propCode == 0xDC08) { // DateCreated
            std::string s;
            if (!ParseStringByType(data.value, off, dataType, s)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "DateCreated parse failed");
            }
            e.ctime_epoch = ParseMtpDateTime(s);
            continue;
        }

        if (propCode == 0xDC09) { // DateModified
            std::string s;
            if (!ParseStringByType(data.value, off, dataType, s)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "DateModified parse failed");
            }
            e.mtime_epoch = ParseMtpDateTime(s);
            continue;
        }

        if (propCode == 0xDC87) { // Width
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "Width parse failed");
            }
            e.image_width = static_cast<uint32_t>(v);
            continue;
        }

        if (propCode == 0xDC88) { // Height
            uint64_t v = 0;
            if (!ParseUnsignedByType(data.value, off, dataType, v)) {
                return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "Height parse failed");
            }
            e.image_height = static_cast<uint32_t>(v);
            continue;
        }

        if (!SkipDataByType(data.value, off, dataType)) {
            return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::ProtocolDesync, "ObjectPropList unsupported data type");
        }
    }

    std::vector<ObjectEntry> out;
    out.reserve(byHandle.size());
    for (auto& kv : byHandle) {
        if (kv.second.name.empty()) {
            kv.second.name = "Object " + std::to_string(kv.second.handle);
        }
        if (kv.second.mtime_epoch == 0) {
            kv.second.mtime_epoch = kv.second.ctime_epoch;
        }
        if (!kv.second.name.empty() && kv.second.name[0] == '.') {
            kv.second.is_hidden = true;
        }
        out.push_back(std::move(kv.second));
    }
    return Result<std::vector<ObjectEntry>>::Success(std::move(out));
}

Result<std::vector<uint8_t>> MtpSession::GetObject(ObjectHandle handle) {
    return _ptp.GetData(ptp::kOpGetObject, {handle});
}

Result<std::vector<uint8_t>> MtpSession::GetPartialObject(ObjectHandle handle, uint32_t offset, uint32_t max_bytes) {
    return _ptp.GetData(ptp::kOpGetPartialObject, {handle, offset, max_bytes});
}

Result<ObjectHandle> MtpSession::SendObjectInfo(const std::string& name,
                                                StorageId storageId,
                                                ObjectHandle parent,
                                                bool is_dir,
                                                uint64_t size) {
    std::vector<uint8_t> info;
    info.reserve(128 + name.size() * 2);

    const uint16_t format = is_dir ? 0x3001 : 0x3000;
    AppendU32(info, storageId);
    AppendU16(info, format);
    AppendU16(info, 0); // protection
    AppendU32(info, static_cast<uint32_t>(size)); // for compatibility
    AppendU16(info, 0); // thumb format
    AppendU32(info, 0); // thumb compressed size
    AppendU32(info, 0); // thumb width
    AppendU32(info, 0); // thumb height
    AppendU32(info, 0); // image width
    AppendU32(info, 0); // image height
    AppendU32(info, 0); // image bit depth
    AppendU32(info, parent);
    AppendU16(info, is_dir ? 1 : 0); // association type
    AppendU32(info, 0); // association desc
    AppendU32(info, 0); // sequence number
    AppendPtpString(info, name);
    AppendPtpString(info, std::string()); // date created
    AppendPtpString(info, std::string()); // date modified
    AppendPtpString(info, std::string()); // keywords

    auto r = _ptp.Transaction(ptp::kOpSendObjectInfo, {storageId, parent}, nullptr, &info);
    if (!r.ok) {
        return Result<ObjectHandle>::Failure(r.code, r.message, r.retryable);
    }
    if (r.value.params.size() < 3) {
        return Result<ObjectHandle>::Failure(ErrorCode::ProtocolDesync, "SendObjectInfo missing response params");
    }
    return Result<ObjectHandle>::Success(r.value.params[2]);
}

Status MtpSession::SendObject(ObjectHandle handle, const std::vector<uint8_t>& payload) {
    auto r = _ptp.Transaction(ptp::kOpSendObject, {handle}, nullptr, &payload);
    if (!r.ok) {
        return Status::Failure(r.code, r.message, r.retryable);
    }
    return OkStatus();
}

Result<ObjectHandle> MtpSession::CreateFolder(const std::string& name, StorageId storageId, ObjectHandle parent) {
    auto folder = SendObjectInfo(name, storageId, parent, true, 0);
    if (!folder.ok) {
        return Result<ObjectHandle>::Failure(folder.code, folder.message, folder.retryable);
    }
    return folder;
}

Status MtpSession::SetObjectFileName(ObjectHandle handle, const std::string& new_name) {
    std::vector<uint8_t> value;
    AppendPtpString(value, new_name);
    auto st = _ptp.SendData(ptp::kOpSetObjectPropValue, {handle, 0xDC07}, value);
    if (!st.ok) {
        return st;
    }
    return OkStatus();
}

Status MtpSession::SetObjectFileNameViaPropList(ObjectHandle handle, const std::string& new_name) {
    std::vector<uint8_t> payload;
    // MTP ObjectPropList dataset with one entry:
    // [count:u32]
    // [objectHandle:u32][propertyCode:u16][dataType:u16][value]
    AppendU32(payload, 1);
    AppendU32(payload, handle);
    AppendU16(payload, 0xDC07); // ObjectFileName
    AppendU16(payload, 0xFFFF); // STR
    AppendPtpString(payload, new_name);

    auto st = _ptp.SendData(ptp::kOpSetObjectPropList, {}, payload);
    if (!st.ok) {
        return st;
    }
    return OkStatus();
}

Result<ObjectHandle> MtpSession::CopyObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent) {
    auto r = _ptp.Transaction(ptp::kOpCopyObject, {handle, storageId, parent}, nullptr, nullptr);
    if (!r.ok) {
        return Result<ObjectHandle>::Failure(r.code, r.message, r.retryable);
    }
    if (!r.value.params.empty()) {
        return Result<ObjectHandle>::Success(r.value.params[0]);
    }
    return Result<ObjectHandle>::Success(0);
}

Status MtpSession::MoveObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent) {
    auto r = _ptp.Transaction(ptp::kOpMoveObject, {handle, storageId, parent}, nullptr, nullptr);
    if (!r.ok) {
        return Status::Failure(r.code, r.message, r.retryable);
    }
    return OkStatus();
}

Status MtpSession::DeleteObject(ObjectHandle handle, uint16_t format) {
    auto r = _ptp.Transaction(ptp::kOpDeleteObject, {handle, format}, nullptr, nullptr);
    if (!r.ok) {
        return Status::Failure(r.code, r.message, r.retryable);
    }
    return OkStatus();
}

CapabilityProfile MtpSession::BuildCapabilityProfile(const DeviceInfoData& info) const {
    CapabilityProfile caps;
    caps.has_get_object_prop_list = info.operations_supported.count(ptp::kOpGetObjectPropList) != 0;
    caps.has_set_object_prop_list = info.operations_supported.count(ptp::kOpSetObjectPropList) != 0;
    caps.has_get_partial_object = info.operations_supported.count(ptp::kOpGetPartialObject) != 0;
    return caps;
}

uint16_t MtpSession::ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t MtpSession::ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t MtpSession::ReadU64(const uint8_t* p) {
    return static_cast<uint64_t>(ReadU32(p)) | (static_cast<uint64_t>(ReadU32(p + 4)) << 32);
}

Result<std::vector<uint16_t>> MtpSession::ParseU16Array(const std::vector<uint8_t>& data, size_t& off) {
    if (off + 4 > data.size()) {
        return Result<std::vector<uint16_t>>::Failure(ErrorCode::ProtocolDesync, "Array count out of range");
    }
    const uint32_t count = ReadU32(data.data() + off);
    off += 4;
    if (off + count * 2 > data.size()) {
        return Result<std::vector<uint16_t>>::Failure(ErrorCode::ProtocolDesync, "U16 array out of range");
    }

    std::vector<uint16_t> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        out.push_back(ReadU16(data.data() + off));
        off += 2;
    }
    return Result<std::vector<uint16_t>>::Success(std::move(out));
}

Result<std::vector<uint32_t>> MtpSession::ParseU32Array(const std::vector<uint8_t>& data, size_t& off) {
    if (off + 4 > data.size()) {
        return Result<std::vector<uint32_t>>::Failure(ErrorCode::ProtocolDesync, "Array count out of range");
    }
    const uint32_t count = ReadU32(data.data() + off);
    off += 4;
    if (off + count * 4 > data.size()) {
        return Result<std::vector<uint32_t>>::Failure(ErrorCode::ProtocolDesync, "U32 array out of range");
    }

    std::vector<uint32_t> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        out.push_back(ReadU32(data.data() + off));
        off += 4;
    }
    return Result<std::vector<uint32_t>>::Success(std::move(out));
}

Result<std::string> MtpSession::ParsePtpString(const std::vector<uint8_t>& data, size_t& off) {
    if (off >= data.size()) {
        return Result<std::string>::Failure(ErrorCode::ProtocolDesync, "String length out of range");
    }

    const uint8_t count = data[off++];
    if (count == 0) {
        return Result<std::string>::Success(std::string());
    }

    const size_t bytes = static_cast<size_t>(count) * 2;
    if (off + bytes > data.size()) {
        return Result<std::string>::Failure(ErrorCode::ProtocolDesync, "String data out of range");
    }

    std::string out;
    out.reserve(count > 0 ? (count - 1) * 2 : 0);
    auto append_utf8 = [&out](uint32_t cp) {
        if (cp <= 0x7f) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | ((cp >> 6) & 0x1f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | ((cp >> 12) & 0x0f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    };

    for (uint8_t i = 0; i + 1 < count; ++i) {
        uint16_t ch = ReadU16(data.data() + off + i * 2);
        if (ch >= 0xD800 && ch <= 0xDBFF && (i + 2) < count) {
            uint16_t lo = ReadU16(data.data() + off + (i + 1) * 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                uint32_t cp = 0x10000u + ((static_cast<uint32_t>(ch - 0xD800) << 10) | (lo - 0xDC00));
                append_utf8(cp);
                ++i;
                continue;
            }
        }
        append_utf8(ch);
    }

    off += bytes;
    return Result<std::string>::Success(std::move(out));
}

void MtpSession::AppendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void MtpSession::AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void MtpSession::AppendU64(std::vector<uint8_t>& out, uint64_t v) {
    AppendU32(out, static_cast<uint32_t>(v & 0xffffffffu));
    AppendU32(out, static_cast<uint32_t>((v >> 32) & 0xffffffffu));
}

void MtpSession::AppendPtpString(std::vector<uint8_t>& out, const std::string& s) {
    std::vector<uint16_t> utf16;
    utf16.reserve(std::min<size_t>(254, s.size()));
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t used = 1;
        if ((c & 0x80) == 0) {
            cp = c;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < s.size()) {
            cp = ((c & 0x1f) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f);
            used = 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < s.size()) {
            cp = ((c & 0x0f) << 12) |
                 ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3f);
            used = 3;
        } else if ((c & 0xf8) == 0xf0 && i + 3 < s.size()) {
            cp = ((c & 0x07) << 18) |
                 ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3f) << 6) |
                 (static_cast<unsigned char>(s[i + 3]) & 0x3f);
            used = 4;
        } else {
            cp = '?';
            used = 1;
        }
        if (cp <= 0xFFFF) {
            utf16.push_back(static_cast<uint16_t>(cp));
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            utf16.push_back(static_cast<uint16_t>(0xD800u + ((cp >> 10) & 0x3FFu)));
            utf16.push_back(static_cast<uint16_t>(0xDC00u + (cp & 0x3FFu)));
        } else {
            utf16.push_back(static_cast<uint16_t>('?'));
        }
        i += used;
        if (utf16.size() >= 254) {
            break;
        }
    }

    const uint8_t count = static_cast<uint8_t>(utf16.size() + 1);
    out.push_back(count);
    for (uint16_t ch : utf16) {
        AppendU16(out, ch);
    }
    AppendU16(out, 0);
}

uint64_t MtpSession::ParseMtpDateTime(const std::string& s) {
    // MTP date format: YYYYMMDDThhmmss
    if (s.size() < 15) {
        return 0;
    }
    std::tm tmv = {};
    try {
        tmv.tm_year = std::stoi(s.substr(0, 4)) - 1900;
        tmv.tm_mon = std::stoi(s.substr(4, 2)) - 1;
        tmv.tm_mday = std::stoi(s.substr(6, 2));
        tmv.tm_hour = std::stoi(s.substr(9, 2));
        tmv.tm_min = std::stoi(s.substr(11, 2));
        tmv.tm_sec = std::stoi(s.substr(13, 2));
    } catch (...) {
        return 0;
    }
    time_t t = mktime(&tmv);
    if (t < 0) {
        return 0;
    }
    return static_cast<uint64_t>(t);
}

bool MtpSession::ParseUnsignedByType(const std::vector<uint8_t>& data, size_t& off, uint16_t dataType, uint64_t& v) {
    if (dataType == 0x0001 || dataType == 0x0002 || dataType == 0x4001 || dataType == 0x4002) {
        if (off + 1 > data.size()) return false;
        v = data[off];
        off += 1;
        return true;
    }
    if (dataType == 0x0003 || dataType == 0x0004 || dataType == 0x4003 || dataType == 0x4004) {
        if (off + 2 > data.size()) return false;
        v = ReadU16(data.data() + off);
        off += 2;
        return true;
    }
    if (dataType == 0x0005 || dataType == 0x0006 || dataType == 0x4005 || dataType == 0x4006) {
        if (off + 4 > data.size()) return false;
        v = ReadU32(data.data() + off);
        off += 4;
        return true;
    }
    if (dataType == 0x0007 || dataType == 0x0008 || dataType == 0x4007 || dataType == 0x4008) {
        if (off + 8 > data.size()) return false;
        v = ReadU64(data.data() + off);
        off += 8;
        return true;
    }
    return false;
}

bool MtpSession::ParseStringByType(const std::vector<uint8_t>& data, size_t& off, uint16_t dataType, std::string& out) {
    if (dataType != 0xFFFF) {
        return false;
    }
    auto s = ParsePtpString(data, off);
    if (!s.ok) {
        return false;
    }
    out = std::move(s.value);
    return true;
}

bool MtpSession::SkipDataByType(const std::vector<uint8_t>& data, size_t& off, uint16_t dataType) {
    uint64_t temp = 0;
    std::string s;
    if (ParseUnsignedByType(data, off, dataType, temp)) {
        return true;
    }
    if (ParseStringByType(data, off, dataType, s)) {
        return true;
    }

    // arrays: high bit group codes in PTP/MTP; minimally parse common arrays.
    if (dataType == 0x400A || dataType == 0x4001 || dataType == 0x4002 || dataType == 0x4003 || dataType == 0x4004 || dataType == 0x4005 || dataType == 0x4006 || dataType == 0x4008) {
        if (off + 4 > data.size()) return false;
        const uint32_t n = ReadU32(data.data() + off);
        off += 4;
        size_t elemSize = 1;
        if (dataType == 0x4003 || dataType == 0x4005) elemSize = 2;
        if (dataType == 0x4004 || dataType == 0x4006) elemSize = 4;
        if (dataType == 0x4008) elemSize = 8;
        if (off + n * elemSize > data.size()) return false;
        off += n * elemSize;
        return true;
    }
    return false;
}

} // namespace proto
