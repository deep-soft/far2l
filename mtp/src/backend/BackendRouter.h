#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "IProtocolSession.h"
#include "MtpBackend.h"
#include "PtpBackend.h"
#include "../mtp/MtpSession.h"
#include "../ptp/PtpSession.h"
#include "../usb/UsbEnumerator.h"
#include "../usb/UsbTransport.h"

namespace proto {

class BackendRouter {
public:
    struct DeviceProbe {
        UsbDeviceCandidate candidate;
        DeviceEntry device;
    };

    Result<std::vector<DeviceProbe>> EnumerateAndClassify();

    std::shared_ptr<IProtocolSession> Acquire(const UsbDeviceCandidate& candidate, BackendKind kind);
    void Release(const std::string& device_key);

private:
    BackendKind ProbeAndClassify(const UsbDeviceCandidate& candidate);

    std::mutex _mtx;
    std::unordered_map<std::string, std::weak_ptr<IProtocolSession>> _claims;
};

} // namespace proto
