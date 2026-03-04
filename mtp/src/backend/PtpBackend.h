#pragma once

#include <memory>

#include "IProtocolSession.h"
#include "../core/OpQueue.h"
#include "../mtp/DirectoryCache.h"
#include "../mtp/MtpDeviceModel.h"
#include "../mtp/MtpSession.h"
#include "../mtp/QuirkRegistry.h"
#include "../ptp/PtpSession.h"
#include "../usb/UsbEnumerator.h"
#include "../usb/UsbTransport.h"

namespace proto {

class PtpBackend final : public IProtocolSession {
public:
    explicit PtpBackend(UsbDeviceCandidate candidate);
    ~PtpBackend() override;

    BackendKind Kind() const override { return BackendKind::PTP; }
    std::string DeviceKey() const override { return _candidate.id.Key(); }

    Status Connect() override;
    void Disconnect() override;
    bool IsReady() const override { return _ready; }

    Result<std::vector<StorageEntry>> ListStorages() override;
    Result<std::vector<ObjectEntry>> ListChildren(StorageId storageId, ObjectHandle parent) override;
    Result<ObjectEntry> Stat(ObjectHandle handle) override;

    Status Download(ObjectHandle handle,
                    const std::string& local_path,
                    std::function<void(uint64_t, uint64_t)> progress,
                    const CancellationToken& cancel) override;

    Status Upload(const std::string& local_path,
                  const std::string& remote_name,
                  StorageId storageId,
                  ObjectHandle parent,
                  std::function<void(uint64_t, uint64_t)> progress,
                  const CancellationToken& cancel) override;
    Status Rename(ObjectHandle handle, const std::string& new_name) override;

    Status Delete(ObjectHandle handle, bool recursive) override;
    Status MakeDirectory(const std::string& name, StorageId storageId, ObjectHandle parent) override;
    void PollEvents() override;

private:
    UsbDeviceCandidate _candidate;
    UsbTransport _transport;
    std::unique_ptr<PtpSession> _ptp;
    std::unique_ptr<MtpSession> _session;
    DirectoryCache _cache;
    QuirkRegistry _quirkRegistry;
    std::unique_ptr<MtpDeviceModel> _model;
    OpQueue _queue;
    bool _ready = false;
};

} // namespace proto
