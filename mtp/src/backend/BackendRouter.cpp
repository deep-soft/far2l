#include "BackendRouter.h"
#include "../MTPLog.h"

#include <algorithm>
#include <cctype>

namespace proto {

Result<std::vector<BackendRouter::DeviceProbe>> BackendRouter::EnumerateAndClassify() {
    UsbEnumerator enumerator;
    auto devices = enumerator.EnumerateStillImaging();
    if (!devices.ok) {
        return Result<std::vector<DeviceProbe>>::Failure(devices.code, devices.message, devices.retryable);
    }

    std::vector<DeviceProbe> out;
    out.reserve(devices.value.size());

    for (const auto& c : devices.value) {
        DeviceProbe p;
        p.candidate = c;
        p.device.id = c.id;
        p.device.key = c.id.Key();
        p.device.serial = c.serial;
        p.device.manufacturer = c.manufacturer;
        p.device.product = c.product;
        p.device.vendor_id = c.vendor_id;
        p.device.product_id = c.product_id;
        p.device.backend = ProbeAndClassify(c);
        DBG("ProbeAndClassify key=%s vid=%04x pid=%04x backend=%d",
            p.device.key.c_str(),
            c.vendor_id,
            c.product_id,
            static_cast<int>(p.device.backend));
        out.push_back(std::move(p));
    }

    return Result<std::vector<DeviceProbe>>::Success(std::move(out));
}

std::shared_ptr<IProtocolSession> BackendRouter::Acquire(const UsbDeviceCandidate& candidate, BackendKind kind) {
    const std::string key = candidate.id.Key();
    DBG("BackendRouter::Acquire key=%s kind=%d vid=%04x pid=%04x if=%u",
        key.c_str(),
        static_cast<int>(kind),
        candidate.vendor_id,
        candidate.product_id,
        candidate.interface_number);

    std::lock_guard<std::mutex> lk(_mtx);
    auto it = _claims.find(key);
    if (it != _claims.end()) {
        auto locked = it->second.lock();
        if (locked) {
            DBG("BackendRouter::Acquire reuse existing backend key=%s kind=%d", key.c_str(), static_cast<int>(locked->Kind()));
            return locked;
        }
    }

    std::shared_ptr<IProtocolSession> backend;
    if (kind == BackendKind::PTP) {
        backend = std::make_shared<PtpBackend>(candidate);
    } else {
        backend = std::make_shared<MtpBackend>(candidate);
    }
    _claims[key] = backend;
    DBG("BackendRouter::Acquire created backend key=%s kind=%d", key.c_str(), static_cast<int>(backend->Kind()));
    return backend;
}

void BackendRouter::Release(const std::string& device_key) {
    std::lock_guard<std::mutex> lk(_mtx);
    DBG("BackendRouter::Release key=%s", device_key.c_str());
    _claims.erase(device_key);
}

BackendKind BackendRouter::ProbeAndClassify(const UsbDeviceCandidate& candidate) {
    UsbTransport transport;

    ClaimMode mode = ClaimMode::Strict;
#if defined(__linux__)
    mode = ClaimMode::AutoDetachKernelDriver;
#endif

    Status opened = transport.Open(candidate, mode);
    if (!opened.ok) {
        DBG("Probe open failed key=%s code=%d msg=%s",
            candidate.id.Key().c_str(),
            static_cast<int>(opened.code),
            opened.message.c_str());
        return BackendKind::Unknown;
    }

    PtpSession ptp(transport);
    MtpSession mtp(ptp);

    auto info = mtp.GetDeviceInfo();
    if (!info.ok) {
        DBG("Probe GetDeviceInfo failed key=%s code=%d msg=%s",
            candidate.id.Key().c_str(),
            static_cast<int>(info.code),
            info.message.c_str());
        transport.Close();
        return BackendKind::Unknown;
    }

    bool mtpHints = false;
    const auto& di = info.value;
    std::string vendDesc = di.vendor_extension_desc;
    std::transform(vendDesc.begin(), vendDesc.end(), vendDesc.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (di.vendor_extension_id == 6) {
        mtpHints = true;
    }

    if (vendDesc.find("microsoft") != std::string::npos ||
        vendDesc.find("mtp") != std::string::npos ||
        di.operations_supported.count(ptp::kOpGetObjectPropsSupported) != 0 ||
        di.operations_supported.count(ptp::kOpGetObjectPropList) != 0) {
        mtpHints = true;
    }

    // iPhone/camera often expose PTP semantics without MTP operation set.
    BackendKind kind = mtpHints ? BackendKind::MTP : BackendKind::PTP;
    DBG("Probe classify key=%s vend_ext_id=%u vend_desc=%s ops=%zu kind=%d",
        candidate.id.Key().c_str(),
        di.vendor_extension_id,
        di.vendor_extension_desc.c_str(),
        di.operations_supported.size(),
        static_cast<int>(kind));
    transport.Close();
    return kind;
}

} // namespace proto
