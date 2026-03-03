#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../core/Cancellation.h"
#include "../core/Types.h"

namespace proto {

class IProtocolSession {
public:
    virtual ~IProtocolSession() = default;

    virtual BackendKind Kind() const = 0;
    virtual std::string DeviceKey() const = 0;

    virtual Status Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsReady() const = 0;

    virtual Result<std::vector<StorageEntry>> ListStorages() = 0;
    virtual Result<std::vector<ObjectEntry>> ListChildren(StorageId storageId, ObjectHandle parent) = 0;
    virtual Result<ObjectEntry> Stat(ObjectHandle handle) = 0;

    virtual Status Download(ObjectHandle handle,
                            const std::string& local_path,
                            std::function<void(uint64_t, uint64_t)> progress,
                            const CancellationToken& cancel) = 0;

    virtual Status Upload(const std::string& local_path,
                          const std::string& remote_name,
                          StorageId storageId,
                          ObjectHandle parent,
                          std::function<void(uint64_t, uint64_t)> progress,
                          const CancellationToken& cancel) = 0;

    virtual Status Rename(ObjectHandle handle, const std::string& new_name) {
        (void)handle;
        (void)new_name;
        return Status::Failure(ErrorCode::Unsupported, "Rename is not supported");
    }

    virtual Result<ObjectHandle> CopyObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent) {
        (void)handle;
        (void)storageId;
        (void)parent;
        return Result<ObjectHandle>::Failure(ErrorCode::Unsupported, "CopyObject is not supported");
    }

    virtual Status MoveObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent) {
        (void)handle;
        (void)storageId;
        (void)parent;
        return Status::Failure(ErrorCode::Unsupported, "MoveObject is not supported");
    }

    virtual Status Delete(ObjectHandle handle, bool recursive) = 0;
    virtual Status MakeDirectory(const std::string& name, StorageId storageId, ObjectHandle parent) = 0;

    virtual void PollEvents() {}
};

} // namespace proto
