#pragma once

#include "../backend/IProtocolSession.h"

namespace proto {

class TransferManager {
public:
    explicit TransferManager(std::shared_ptr<IProtocolSession> backend)
        : _backend(std::move(backend)) {}

    void SetBackend(std::shared_ptr<IProtocolSession> backend) {
        _backend = std::move(backend);
    }

    Status Download(ObjectHandle handle,
                    const std::string& local_path,
                    std::function<void(uint64_t, uint64_t)> progress,
                    const CancellationToken& cancel) {
        if (!_backend) {
            return Status::Failure(ErrorCode::Disconnected, "No active backend");
        }
        return _backend->Download(handle, local_path, std::move(progress), cancel);
    }

    Status Upload(const std::string& local_path,
                  const std::string& remote_name,
                  StorageId storageId,
                  ObjectHandle parent,
                  std::function<void(uint64_t, uint64_t)> progress,
                  const CancellationToken& cancel) {
        if (!_backend) {
            return Status::Failure(ErrorCode::Disconnected, "No active backend");
        }
        return _backend->Upload(local_path, remote_name, storageId, parent, std::move(progress), cancel);
    }

private:
    std::shared_ptr<IProtocolSession> _backend;
};

} // namespace proto
