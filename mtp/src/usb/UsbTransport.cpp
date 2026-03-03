#include "UsbTransport.h"

#include <algorithm>

namespace proto {

UsbTransport::UsbTransport() {
    libusb_init(&_ctx);
}

UsbTransport::~UsbTransport() {
    Close();
    if (_ctx) {
        libusb_exit(_ctx);
        _ctx = nullptr;
    }
}

Status UsbTransport::Open(const UsbDeviceCandidate& candidate, ClaimMode mode) {
    Close();
    _candidate = candidate;

    libusb_device** list = nullptr;
    ssize_t cnt = libusb_get_device_list(_ctx, &list);
    if (cnt < 0) {
        return Status::Failure(ErrorCode::Io, "libusb_get_device_list failed");
    }

    libusb_device* found = nullptr;
    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device* dev = list[i];
        if (libusb_get_bus_number(dev) == _candidate.id.bus &&
            libusb_get_device_address(dev) == _candidate.id.address) {
            found = dev;
            break;
        }
    }

    if (!found) {
        libusb_free_device_list(list, 1);
        return Status::Failure(ErrorCode::NotFound, "USB device not found");
    }

    if (libusb_open(found, &_handle) != 0 || !_handle) {
        libusb_free_device_list(list, 1);
        return Status::Failure(ErrorCode::AccessDenied, "Failed to open USB device");
    }

    if (mode == ClaimMode::AutoDetachKernelDriver) {
        libusb_set_auto_detach_kernel_driver(_handle, 1);
    }

    if (_candidate.config_value != 0) {
        int cfg = -1;
        if (libusb_get_configuration(_handle, &cfg) == 0 && cfg != _candidate.config_value) {
            (void)libusb_set_configuration(_handle, _candidate.config_value);
        }
    }

    int rc = libusb_claim_interface(_handle, _candidate.interface_number);
    if (rc != 0) {
        libusb_close(_handle);
        _handle = nullptr;
        libusb_free_device_list(list, 1);
        return Status::Failure(ErrorCode::Busy, "Failed to claim USB interface", true);
    }

    if (_candidate.alternate_setting != 0) {
        (void)libusb_set_interface_alt_setting(_handle, _candidate.interface_number, _candidate.alternate_setting);
    }

    _claimed = true;
    libusb_free_device_list(list, 1);
    return OkStatus();
}

void UsbTransport::Close() {
    if (_handle) {
        if (_claimed) {
            (void)libusb_release_interface(_handle, _candidate.interface_number);
            _claimed = false;
        }
        libusb_close(_handle);
        _handle = nullptr;
    }
}

Status UsbTransport::BulkOut(const uint8_t* data, size_t len, int timeout_ms) {
    if (!_handle) {
        return Status::Failure(ErrorCode::Disconnected, "USB device not open");
    }
    int transferred = 0;
    int rc = libusb_bulk_transfer(_handle,
                                  _candidate.endpoint_bulk_out,
                                  const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data)),
                                  static_cast<int>(len),
                                  &transferred,
                                  timeout_ms);
    if (rc == 0 && transferred == static_cast<int>(len)) {
        return OkStatus();
    }
    if (rc == LIBUSB_ERROR_TIMEOUT) {
        return Status::Failure(ErrorCode::Timeout, "USB bulk OUT timeout", true);
    }
    if (rc == LIBUSB_ERROR_PIPE) {
        return Status::Failure(ErrorCode::Io, "USB bulk OUT stall", true);
    }
    if (rc == LIBUSB_ERROR_NO_DEVICE) {
        return Status::Failure(ErrorCode::Disconnected, "USB device disconnected");
    }
    return Status::Failure(ErrorCode::Io, "USB bulk OUT failed");
}

Result<std::vector<uint8_t>> UsbTransport::BulkIn(size_t expected_len_or_max, int timeout_ms) {
    if (!_handle) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::Disconnected, "USB device not open");
    }

    std::vector<uint8_t> out(expected_len_or_max);
    int transferred = 0;
    int rc = libusb_bulk_transfer(_handle,
                                  _candidate.endpoint_bulk_in,
                                  reinterpret_cast<unsigned char*>(out.data()),
                                  static_cast<int>(out.size()),
                                  &transferred,
                                  timeout_ms);
    if (rc != 0) {
        if (rc == LIBUSB_ERROR_TIMEOUT) {
            return Result<std::vector<uint8_t>>::Failure(ErrorCode::Timeout, "USB bulk IN timeout", true);
        }
        if (rc == LIBUSB_ERROR_PIPE) {
            return Result<std::vector<uint8_t>>::Failure(ErrorCode::Io, "USB bulk IN stall", true);
        }
        if (rc == LIBUSB_ERROR_NO_DEVICE) {
            return Result<std::vector<uint8_t>>::Failure(ErrorCode::Disconnected, "USB device disconnected");
        }
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::Io, "USB bulk IN failed");
    }

    out.resize(std::max(0, transferred));
    return Result<std::vector<uint8_t>>::Success(std::move(out));
}

Result<std::vector<uint8_t>> UsbTransport::InterruptIn(size_t max_len, int timeout_ms) {
    if (!_handle || _candidate.endpoint_interrupt_in == 0) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::Unsupported, "No interrupt endpoint");
    }

    std::vector<uint8_t> out(max_len);
    int transferred = 0;
    int rc = libusb_interrupt_transfer(_handle,
                                       _candidate.endpoint_interrupt_in,
                                       reinterpret_cast<unsigned char*>(out.data()),
                                       static_cast<int>(out.size()),
                                       &transferred,
                                       timeout_ms);

    if (rc == 0) {
        out.resize(std::max(0, transferred));
        return Result<std::vector<uint8_t>>::Success(std::move(out));
    }
    if (rc == LIBUSB_ERROR_TIMEOUT) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::Timeout, "USB interrupt timeout", true);
    }
    if (rc == LIBUSB_ERROR_NO_DEVICE) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::Disconnected, "USB device disconnected");
    }
    return Result<std::vector<uint8_t>>::Failure(ErrorCode::Io, "USB interrupt transfer failed");
}

Status UsbTransport::ClearHalt(uint8_t endpoint) {
    if (!_handle) {
        return Status::Failure(ErrorCode::Disconnected, "USB device not open");
    }
    int rc = libusb_clear_halt(_handle, endpoint);
    if (rc == 0) {
        return OkStatus();
    }
    return Status::Failure(ErrorCode::Io, "clear_halt failed", true);
}

Status UsbTransport::RecoverStall() {
    if (!_handle) {
        return Status::Failure(ErrorCode::Disconnected, "USB device not open");
    }

    bool ok = true;
    if (_candidate.endpoint_bulk_in != 0) {
        ok = (libusb_clear_halt(_handle, _candidate.endpoint_bulk_in) == 0) && ok;
    }
    if (_candidate.endpoint_bulk_out != 0) {
        ok = (libusb_clear_halt(_handle, _candidate.endpoint_bulk_out) == 0) && ok;
    }

    if (ok) {
        return OkStatus();
    }
    return Status::Failure(ErrorCode::Io, "Failed to recover stalled bulk endpoints", true);
}

Status UsbTransport::ResetDevice() {
    if (!_handle) {
        return Status::Failure(ErrorCode::Disconnected, "USB device not open");
    }
    int rc = libusb_reset_device(_handle);
    if (rc == 0) {
        return OkStatus();
    }
    return Status::Failure(ErrorCode::Io, "USB device reset failed");
}

} // namespace proto
