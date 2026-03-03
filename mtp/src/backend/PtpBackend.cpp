#include "PtpBackend.h"
#include "../MTPLog.h"

#include <algorithm>
#include <fstream>
#include <sys/stat.h>

namespace proto {

PtpBackend::PtpBackend(UsbDeviceCandidate candidate)
    : _candidate(std::move(candidate)) {
}

PtpBackend::~PtpBackend() {
    Disconnect();
}

Status PtpBackend::Connect() {
    return _queue.Submit([this]() {
        DBG("PtpBackend::Connect key=%s", DeviceKey().c_str());
        if (_ready) {
            return OkStatus();
        }

        ClaimMode mode = ClaimMode::Strict;
#if defined(__linux__)
        mode = ClaimMode::AutoDetachKernelDriver;
#endif

        auto open = _transport.Open(_candidate, mode);
        if (!open.ok) {
            return open;
        }

        _ptp.reset(new PtpSession(_transport));
        _session.reset(new MtpSession(*_ptp));
        _model.reset(new MtpDeviceModel(DeviceKey(),
                                        _candidate.vendor_id,
                                        _candidate.product_id,
                                        *_session,
                                        _cache,
                                        _quirkRegistry));

        auto os = _ptp->OpenSession();
        if (!os.ok) {
            _transport.Close();
            return os;
        }

        auto rd = _model->RefreshDeviceInfo();
        if (!rd.ok) {
            _transport.Close();
            return rd;
        }

        _ready = true;
        return OkStatus();
    });
}

void PtpBackend::Disconnect() {
    _queue.Submit([this]() {
        if (_ptp) {
            (void)_ptp->CloseSession();
        }
        _model.reset();
        _session.reset();
        _ptp.reset();
        _transport.Close();
        _cache.Clear();
        _ready = false;
        return true;
    });
}

Result<std::vector<StorageEntry>> PtpBackend::ListStorages() {
    return _queue.Submit([this]() -> Result<std::vector<StorageEntry>> {
        if (!_ready || !_model) {
            return Result<std::vector<StorageEntry>>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        return _model->RefreshStorages();
    });
}

Result<std::vector<ObjectEntry>> PtpBackend::ListChildren(StorageId storageId, ObjectHandle parent) {
    return _queue.Submit([this, storageId, parent]() -> Result<std::vector<ObjectEntry>> {
        if (!_ready || !_model) {
            return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        return _model->ListChildren(storageId, parent);
    });
}

Result<ObjectEntry> PtpBackend::Stat(ObjectHandle handle) {
    return _queue.Submit([this, handle]() -> Result<ObjectEntry> {
        if (!_ready || !_model) {
            return Result<ObjectEntry>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        return _model->Stat(handle);
    });
}

Status PtpBackend::Download(ObjectHandle handle,
                            const std::string& local_path,
                            std::function<void(uint64_t, uint64_t)> progress,
                            const CancellationToken& cancel) {
    return _queue.Submit([this, handle, local_path, progress, cancel]() -> Status {
        if (!_ready || !_session || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }

        auto obj = _model->Stat(handle);
        if (!obj.ok) {
            return Status::Failure(obj.code, obj.message, obj.retryable);
        }

        auto bytes = _session->GetObject(handle);
        if (!bytes.ok) {
            return Status::Failure(bytes.code, bytes.message, bytes.retryable);
        }
        if (cancel.IsCancelled()) {
            return Status::Failure(ErrorCode::Cancelled, "Cancelled by user");
        }

        std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return Status::Failure(ErrorCode::Io, "Cannot create local file");
        }
        out.write(reinterpret_cast<const char*>(bytes.value.data()), static_cast<std::streamsize>(bytes.value.size()));
        if (!out.good()) {
            return Status::Failure(ErrorCode::Io, "Local write failed");
        }

        if (progress) {
            progress(bytes.value.size(), obj.value.size);
        }
        return OkStatus();
    });
}

Status PtpBackend::Upload(const std::string&,
                          const std::string&,
                          StorageId,
                          ObjectHandle,
                          std::function<void(uint64_t, uint64_t)>,
                          const CancellationToken&) {
    return Status::Failure(ErrorCode::Unsupported, "PTP backend does not implement upload yet");
}

Status PtpBackend::Rename(ObjectHandle, const std::string&) {
    return Status::Failure(ErrorCode::Unsupported, "PTP backend does not implement rename");
}

Status PtpBackend::Delete(ObjectHandle, bool) {
    return Status::Failure(ErrorCode::Unsupported, "PTP backend does not implement delete");
}

Status PtpBackend::MakeDirectory(const std::string&, StorageId, ObjectHandle) {
    return Status::Failure(ErrorCode::Unsupported, "PTP backend does not implement mkdir");
}

void PtpBackend::PollEvents() {
    _queue.Submit([this]() {
        if (!_ready) {
            return true;
        }
        auto ev = _transport.InterruptIn(512, 5);
        if (ev.ok && !ev.value.empty()) {
            _cache.InvalidateDevice(DeviceKey());
        } else if (!ev.ok && ev.code == ErrorCode::Disconnected) {
            _cache.InvalidateDevice(DeviceKey());
            if (_ptp) {
                (void)_ptp->CloseSession();
            }
            _model.reset();
            _session.reset();
            _ptp.reset();
            _transport.Close();
            _ready = false;
        }
        return true;
    });
}

} // namespace proto
