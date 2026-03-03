#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <libusb.h>

#include "../core/Types.h"

namespace proto {

struct UsbDeviceCandidate {
    DeviceId id;
    uint16_t vendor_id = 0;
    uint16_t product_id = 0;
    std::string serial;
    std::string manufacturer;
    std::string product;

    uint8_t config_value = 0;
    uint8_t interface_number = 0;
    uint8_t alternate_setting = 0;
    uint8_t endpoint_bulk_in = 0;
    uint8_t endpoint_bulk_out = 0;
    uint8_t endpoint_interrupt_in = 0;
    uint16_t max_packet_in = 0;
    uint16_t max_packet_out = 0;
};

class UsbEnumerator {
public:
    UsbEnumerator();
    ~UsbEnumerator();

    Result<std::vector<UsbDeviceCandidate>> EnumerateStillImaging();

private:
    std::string ReadString(libusb_device_handle* h, uint8_t idx);

    libusb_context* _ctx = nullptr;
};

} // namespace proto
