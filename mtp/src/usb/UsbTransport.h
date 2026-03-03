#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <libusb.h>

#include "../core/Types.h"
#include "UsbEnumerator.h"

namespace proto {

enum class ClaimMode {
    Strict,
    AutoDetachKernelDriver,
};

class UsbTransport {
public:
    UsbTransport();
    ~UsbTransport();

    Status Open(const UsbDeviceCandidate& candidate, ClaimMode mode);
    void Close();

    Status BulkOut(const uint8_t* data, size_t len, int timeout_ms);
    Result<std::vector<uint8_t>> BulkIn(size_t expected_len_or_max, int timeout_ms);
    Result<std::vector<uint8_t>> InterruptIn(size_t max_len, int timeout_ms);

    Status ClearHalt(uint8_t endpoint);
    Status RecoverStall();
    Status ResetDevice();

private:
    libusb_context* _ctx = nullptr;
    libusb_device_handle* _handle = nullptr;
    UsbDeviceCandidate _candidate;
    bool _claimed = false;
};

} // namespace proto
