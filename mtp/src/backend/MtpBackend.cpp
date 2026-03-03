#include "MtpBackend.h"
#include "../MTPLog.h"

#include <cerrno>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace proto {

MtpBackend::MtpBackend(UsbDeviceCandidate candidate)
    : _candidate(std::move(candidate)) {
}

MtpBackend::~MtpBackend() {
    Disconnect();
}

Status MtpBackend::Connect() {
    return _queue.Submit([this]() {
        DBG("MtpBackend::Connect key=%s", DeviceKey().c_str());
        if (_ready) {
            return OkStatus();
        }

        ClaimMode mode = ClaimMode::Strict;
#if defined(__linux__)
        mode = ClaimMode::AutoDetachKernelDriver;
#endif

        Status open = _transport.Open(_candidate, mode);
        if (!open.ok) {
            DBG("transport open failed code=%d msg=%s", static_cast<int>(open.code), open.message.c_str());
            return open;
        }

        _ptp.reset(new PtpSession(_transport));
        _mtp.reset(new MtpSession(*_ptp));
        _model.reset(new MtpDeviceModel(DeviceKey(),
                                        _candidate.vendor_id,
                                        _candidate.product_id,
                                        *_mtp,
                                        _cache,
                                        _quirkRegistry));

        // Probe device before opening a session to classify basic compatibility.
        auto info = _mtp->GetDeviceInfo();
        if (!info.ok) {
            DBG("GetDeviceInfo failed code=%d msg=%s", static_cast<int>(info.code), info.message.c_str());
            _transport.Close();
            return Status::Failure(info.code, info.message, info.retryable);
        }

        Status os = _ptp->OpenSession();
        if (!os.ok) {
            DBG("OpenSession failed code=%d msg=%s", static_cast<int>(os.code), os.message.c_str());
            _transport.Close();
            return os;
        }

        Status rd = _model->RefreshDeviceInfo();
        if (!rd.ok) {
            DBG("RefreshDeviceInfo failed code=%d msg=%s", static_cast<int>(rd.code), rd.message.c_str());
            _transport.Close();
            return rd;
        }

        _ready = true;
        DBG("MtpBackend::Connect success key=%s", DeviceKey().c_str());
        return OkStatus();
    });
}

void MtpBackend::Disconnect() {
    _queue.Submit([this]() {
        if (_ptp) {
            (void)_ptp->CloseSession();
        }
        _model.reset();
        _mtp.reset();
        _ptp.reset();
        _transport.Close();
        _cache.Clear();
        _ready = false;
        return true;
    });
}

Result<std::vector<StorageEntry>> MtpBackend::ListStorages() {
    return _queue.Submit([this]() -> Result<std::vector<StorageEntry>> {
        if (!_ready || !_model) {
            return Result<std::vector<StorageEntry>>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        auto out = _model->RefreshStorages();
        if (!out.ok) {
            DBG("ListStorages failed code=%d msg=%s", static_cast<int>(out.code), out.message.c_str());
        } else {
            DBG("ListStorages success count=%zu", out.value.size());
        }
        return out;
    });
}

Result<std::vector<ObjectEntry>> MtpBackend::ListChildren(StorageId storageId, ObjectHandle parent) {
    return _queue.Submit([this, storageId, parent]() -> Result<std::vector<ObjectEntry>> {
        if (!_ready || !_model) {
            return Result<std::vector<ObjectEntry>>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        return _model->ListChildren(storageId, parent);
    });
}

Result<ObjectEntry> MtpBackend::Stat(ObjectHandle handle) {
    return _queue.Submit([this, handle]() -> Result<ObjectEntry> {
        if (!_ready || !_model) {
            return Result<ObjectEntry>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        return _model->Stat(handle);
    });
}

Status MtpBackend::Download(ObjectHandle handle,
                            const std::string& local_path,
                            std::function<void(uint64_t, uint64_t)> progress,
                            const CancellationToken& cancel) {
    return _queue.Submit([this, handle, local_path, progress, cancel]() -> Status {
        if (!_ready || !_mtp || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }

        auto obj = _model->Stat(handle);
        if (!obj.ok) {
            return Status::Failure(obj.code, obj.message, obj.retryable);
        }

        const uint64_t totalSize = obj.value.size;
        const bool allowPartial = _model->Capabilities().has_get_partial_object && !_model->Quirks().no_partial_object;

        uint64_t startOffset = 0;
        struct stat localSt = {};
        if (stat(local_path.c_str(), &localSt) == 0 && S_ISREG(localSt.st_mode)) {
            if (static_cast<uint64_t>(localSt.st_size) < totalSize && allowPartial) {
                startOffset = static_cast<uint64_t>(localSt.st_size);
            } else if (static_cast<uint64_t>(localSt.st_size) > totalSize) {
                std::ofstream trunc(local_path, std::ios::binary | std::ios::trunc);
                if (!trunc.is_open()) {
                    return Status::Failure(ErrorCode::Io, "Cannot truncate local file");
                }
                trunc.close();
                startOffset = 0;
            }
        }

        std::ofstream out(local_path, startOffset > 0 ? (std::ios::binary | std::ios::app) : (std::ios::binary | std::ios::trunc));
        if (!out.is_open()) {
            return Status::Failure(ErrorCode::Io, "Cannot create local file");
        }

        if (allowPartial) {
            constexpr uint32_t kChunk = 1024 * 1024;
            uint64_t offset = startOffset;
            while (offset < totalSize) {
                if (cancel.IsCancelled()) {
                    return Status::Failure(ErrorCode::Cancelled, "Cancelled by user");
                }

                uint32_t want = static_cast<uint32_t>(std::min<uint64_t>(kChunk, totalSize - offset));
                Result<std::vector<uint8_t>> part = Result<std::vector<uint8_t>>::Failure(ErrorCode::Io, "not started");
                bool got = false;
                for (int attempt = 0; attempt < 3 && !got; ++attempt) {
                    part = _mtp->GetPartialObject(handle, static_cast<uint32_t>(offset), want);
                    if (part.ok) {
                        got = true;
                    } else if (!part.retryable) {
                        break;
                    }
                }
                if (!part.ok) {
                    return Status::Failure(part.code, part.message, part.retryable);
                }
                if (part.value.empty()) {
                    return Status::Failure(ErrorCode::ProtocolDesync, "Partial object returned empty chunk");
                }

                out.write(reinterpret_cast<const char*>(part.value.data()), static_cast<std::streamsize>(part.value.size()));
                if (!out.good()) {
                    return Status::Failure(ErrorCode::Io, "Local write failed");
                }
                offset += part.value.size();
                if (progress) {
                    progress(offset, totalSize);
                }
            }
            out.close();
            return OkStatus();
        }

        auto bytes = _mtp->GetObject(handle);
        if (!bytes.ok) {
            return Status::Failure(bytes.code, bytes.message, bytes.retryable);
        }
        if (cancel.IsCancelled()) {
            return Status::Failure(ErrorCode::Cancelled, "Cancelled by user");
        }
        out.write(reinterpret_cast<const char*>(bytes.value.data()), static_cast<std::streamsize>(bytes.value.size()));
        if (!out.good()) {
            return Status::Failure(ErrorCode::Io, "Local write failed");
        }
        if (progress) {
            progress(bytes.value.size(), bytes.value.size());
        }
        out.close();
        return OkStatus();
    });
}

Status MtpBackend::Upload(const std::string& local_path,
                          const std::string& remote_name,
                          StorageId storageId,
                          ObjectHandle parent,
                          std::function<void(uint64_t, uint64_t)> progress,
                          const CancellationToken& cancel) {
    return _queue.Submit([this, local_path, remote_name, storageId, parent, progress, cancel]() -> Status {
        if (!_ready || !_mtp || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }

        std::function<Status(const std::string&, const std::string&, StorageId, ObjectHandle)> uploadRec;
        uploadRec = [&](const std::string& srcPath, const std::string& dstName, StorageId sid, ObjectHandle dstParent) -> Status {
            if (cancel.IsCancelled()) {
                return Status::Failure(ErrorCode::Cancelled, "Cancelled by user");
            }

            struct stat st = {};
            if (stat(srcPath.c_str(), &st) != 0) {
                return Status::Failure(ErrorCode::NotFound, "Source path not found");
            }

            if (S_ISDIR(st.st_mode)) {
                auto existingDir = _model->FindChildByName(sid, dstParent, dstName);
                Result<ObjectHandle> folder = Result<ObjectHandle>::Failure(ErrorCode::NotFound, "Folder not found");
                if (existingDir.ok && existingDir.value.is_dir) {
                    folder = Result<ObjectHandle>::Success(existingDir.value.handle);
                } else {
                    folder = _mtp->CreateFolder(dstName, sid, dstParent);
                    if (!folder.ok) {
                        return Status::Failure(folder.code, folder.message, folder.retryable);
                    }
                }

                DIR* dir = opendir(srcPath.c_str());
                if (!dir) {
                    return Status::Failure(ErrorCode::Io, "Failed to open source directory");
                }

                Status status = OkStatus();
                while (true) {
                    dirent* ent = readdir(dir);
                    if (!ent) {
                        break;
                    }
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                        continue;
                    }
                    std::string childSrc = srcPath + "/" + ent->d_name;
                    status = uploadRec(childSrc, ent->d_name, sid, folder.value);
                    if (!status.ok) {
                        break;
                    }
                }
                closedir(dir);
                _model->InvalidateOnMutation(sid, dstParent);
                return status;
            }

            std::ifstream in(srcPath, std::ios::binary | std::ios::ate);
            if (!in.is_open()) {
                return Status::Failure(ErrorCode::Io, "Failed to open source file");
            }
            std::streamsize sz = in.tellg();
            in.seekg(0, std::ios::beg);

            std::vector<uint8_t> payload(static_cast<size_t>(sz));
            if (sz > 0 && !in.read(reinterpret_cast<char*>(payload.data()), sz)) {
                return Status::Failure(ErrorCode::Io, "Failed to read source file");
            }

            Result<ObjectHandle> handle = Result<ObjectHandle>::Failure(ErrorCode::Io, "SendObjectInfo not started");
            for (int attempt = 0; attempt < 3; ++attempt) {
                handle = _mtp->SendObjectInfo(dstName, sid, dstParent, false, static_cast<uint64_t>(payload.size()));
                if (handle.ok) {
                    break;
                }
                auto existing = _model->FindChildByName(sid, dstParent, dstName);
                if (existing.ok && !existing.value.is_dir && existing.value.size == payload.size()) {
                    return OkStatus();
                }
                if (!handle.retryable) {
                    break;
                }
            }
            if (!handle.ok) {
                return Status::Failure(handle.code, handle.message, handle.retryable);
            }

            Status sent = _mtp->SendObject(handle.value, payload);
            if (!sent.ok) {
                auto written = _model->Stat(handle.value);
                if (written.ok && !written.value.is_dir && written.value.size == payload.size()) {
                    return OkStatus();
                }
                return sent;
            }

            if (progress) {
                progress(static_cast<uint64_t>(payload.size()), static_cast<uint64_t>(payload.size()));
            }

            _model->InvalidateOnMutation(sid, dstParent);
            return OkStatus();
        };

        return uploadRec(local_path, remote_name, storageId, parent);
    });
}

Status MtpBackend::Rename(ObjectHandle handle, const std::string& new_name) {
    return _queue.Submit([this, handle, new_name]() -> Status {
        if (!_ready || !_mtp || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        auto stObj = _model->Stat(handle);
        if (!stObj.ok) {
            return Status::Failure(stObj.code, stObj.message, stObj.retryable);
        }
        Status st;
        if (_model->Capabilities().has_set_object_prop_list) {
            st = _mtp->SetObjectFileNameViaPropList(handle, new_name);
            if (!st.ok) {
                st = _mtp->SetObjectFileName(handle, new_name);
            }
        } else {
            st = _mtp->SetObjectFileName(handle, new_name);
        }
        if (!st.ok) {
            return st;
        }
        _model->InvalidateOnMutation(stObj.value.storage_id, stObj.value.parent);
        return OkStatus();
    });
}

Result<ObjectHandle> MtpBackend::CopyObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent) {
    return _queue.Submit([this, handle, storageId, parent]() -> Result<ObjectHandle> {
        if (!_ready || !_mtp || !_model) {
            return Result<ObjectHandle>::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        auto cp = _mtp->CopyObject(handle, storageId, parent);
        if (!cp.ok) {
            return Result<ObjectHandle>::Failure(cp.code, cp.message, cp.retryable);
        }
        _model->InvalidateOnMutation(storageId, parent);
        return cp;
    });
}

Status MtpBackend::MoveObject(ObjectHandle handle, StorageId storageId, ObjectHandle parent) {
    return _queue.Submit([this, handle, storageId, parent]() -> Status {
        if (!_ready || !_mtp || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        auto stObj = _model->Stat(handle);
        if (!stObj.ok) {
            return Status::Failure(stObj.code, stObj.message, stObj.retryable);
        }
        auto mv = _mtp->MoveObject(handle, storageId, parent);
        if (!mv.ok) {
            return mv;
        }
        _model->InvalidateOnMutation(stObj.value.storage_id, stObj.value.parent);
        _model->InvalidateOnMutation(storageId, parent);
        return OkStatus();
    });
}

Status MtpBackend::Delete(ObjectHandle handle, bool recursive) {
    return _queue.Submit([this, handle, recursive]() -> Status {
        if (!_ready || !_mtp || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }

        std::function<Status(ObjectHandle)> deleteRec = [&](ObjectHandle h) -> Status {
            auto stObj = _model->Stat(h);
            if (!stObj.ok) {
                return Status::Failure(stObj.code, stObj.message, stObj.retryable);
            }

            if (recursive && stObj.value.is_dir) {
                auto children = _model->ListChildren(stObj.value.storage_id, h);
                if (!children.ok) {
                    return Status::Failure(children.code, children.message, children.retryable);
                }
                for (const auto& ch : children.value) {
                    auto ds = deleteRec(ch.handle);
                    if (!ds.ok) {
                        return ds;
                    }
                }
            }

            auto del = _mtp->DeleteObject(h, 0);
            if (!del.ok) {
                return del;
            }
            _model->InvalidateOnMutation(stObj.value.storage_id, stObj.value.parent);
            return OkStatus();
        };

        auto st = deleteRec(handle);
        if (!st.ok) {
            return st;
        }
        return OkStatus();
    });
}

Status MtpBackend::MakeDirectory(const std::string& name, StorageId storageId, ObjectHandle parent) {
    return _queue.Submit([this, name, storageId, parent]() -> Status {
        if (!_ready || !_mtp || !_model) {
            return Status::Failure(ErrorCode::Disconnected, "Session not ready");
        }
        auto mk = _mtp->CreateFolder(name, storageId, parent);
        if (!mk.ok) {
            return Status::Failure(mk.code, mk.message, mk.retryable);
        }
        _model->InvalidateOnMutation(storageId, parent);
        return OkStatus();
    });
}

void MtpBackend::PollEvents() {
    _queue.Submit([this]() {
        if (!_ready) {
            return true;
        }
        auto ev = _transport.InterruptIn(512, 5);
        if (ev.ok && !ev.value.empty()) {
            DBG("Interrupt event received bytes=%zu, invalidating cache", ev.value.size());
            _cache.InvalidateDevice(DeviceKey());
        } else if (!ev.ok && ev.code == ErrorCode::Disconnected) {
            DBG("Interrupt poll detected disconnect, tearing down key=%s", DeviceKey().c_str());
            _cache.InvalidateDevice(DeviceKey());
            if (_ptp) {
                (void)_ptp->CloseSession();
            }
            _model.reset();
            _mtp.reset();
            _ptp.reset();
            _transport.Close();
            _ready = false;
        }
        return true;
    });
}

} // namespace proto
