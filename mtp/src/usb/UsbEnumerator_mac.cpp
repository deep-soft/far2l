#include "UsbEnumerator.h"
#include "../MTPLog.h"

#include <vector>

namespace proto {

UsbEnumerator::UsbEnumerator() {
    libusb_init(&_ctx);
}

UsbEnumerator::~UsbEnumerator() {
    if (_ctx) {
        libusb_exit(_ctx);
        _ctx = nullptr;
    }
}

std::string UsbEnumerator::ReadString(libusb_device_handle* h, uint8_t idx) {
    if (!h || idx == 0) {
        return std::string();
    }

    unsigned char buf[256] = {0};
    int n = libusb_get_string_descriptor_ascii(h, idx, buf, sizeof(buf));
    if (n <= 0) {
        return std::string();
    }
    return std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
}

Result<std::vector<UsbDeviceCandidate>> UsbEnumerator::EnumerateStillImaging() {
    if (!_ctx) {
        return Result<std::vector<UsbDeviceCandidate>>::Failure(ErrorCode::Io, "libusb context not initialized");
    }

    libusb_device** list = nullptr;
    ssize_t cnt = libusb_get_device_list(_ctx, &list);
    if (cnt < 0) {
        return Result<std::vector<UsbDeviceCandidate>>::Failure(ErrorCode::Io, "libusb_get_device_list failed");
    }

    std::vector<UsbDeviceCandidate> out;

    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor dd = {};
        if (libusb_get_device_descriptor(dev, &dd) != 0) {
            continue;
        }

        libusb_config_descriptor* cfg = nullptr;
        if (libusb_get_active_config_descriptor(dev, &cfg) != 0) {
            if (libusb_get_config_descriptor(dev, 0, &cfg) != 0) {
                continue;
            }
        }

        libusb_device_handle* handle = nullptr;
        (void)libusb_open(dev, &handle);

        for (uint8_t ifi = 0; ifi < cfg->bNumInterfaces; ++ifi) {
            const libusb_interface& iface = cfg->interface[ifi];
            for (int alt = 0; alt < iface.num_altsetting; ++alt) {
                const libusb_interface_descriptor& id = iface.altsetting[alt];
                if (id.bInterfaceClass != 0x06) {
                    continue;
                }

                UsbDeviceCandidate c;
                c.id.bus = libusb_get_bus_number(dev);
                c.id.address = libusb_get_device_address(dev);
                c.id.interface_number = id.bInterfaceNumber;
                c.vendor_id = dd.idVendor;
                c.product_id = dd.idProduct;
                c.config_value = cfg->bConfigurationValue;
                c.interface_number = id.bInterfaceNumber;
                c.alternate_setting = id.bAlternateSetting;

                c.serial = ReadString(handle, dd.iSerialNumber);
                c.manufacturer = ReadString(handle, dd.iManufacturer);
                c.product = ReadString(handle, dd.iProduct);

                for (uint8_t epi = 0; epi < id.bNumEndpoints; ++epi) {
                    const libusb_endpoint_descriptor& ep = id.endpoint[epi];
                    const uint8_t attr = ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
                    const bool in = (ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN;
                    if (attr == LIBUSB_TRANSFER_TYPE_BULK) {
                        if (in && c.endpoint_bulk_in == 0) {
                            c.endpoint_bulk_in = ep.bEndpointAddress;
                            c.max_packet_in = ep.wMaxPacketSize;
                        } else if (!in && c.endpoint_bulk_out == 0) {
                            c.endpoint_bulk_out = ep.bEndpointAddress;
                            c.max_packet_out = ep.wMaxPacketSize;
                        }
                    } else if (attr == LIBUSB_TRANSFER_TYPE_INTERRUPT && in && c.endpoint_interrupt_in == 0) {
                        c.endpoint_interrupt_in = ep.bEndpointAddress;
                    }
                }

                if (c.endpoint_bulk_in != 0 && c.endpoint_bulk_out != 0) {
                    DBG("UsbEnumerator candidate key=%s vid=%04x pid=%04x class=0x%02x sub=0x%02x proto=0x%02x if=%u alt=%u ep_in=0x%02x ep_out=0x%02x ep_int=0x%02x product=%s serial=%s",
                        c.id.Key().c_str(),
                        c.vendor_id,
                        c.product_id,
                        id.bInterfaceClass,
                        id.bInterfaceSubClass,
                        id.bInterfaceProtocol,
                        c.interface_number,
                        c.alternate_setting,
                        c.endpoint_bulk_in,
                        c.endpoint_bulk_out,
                        c.endpoint_interrupt_in,
                        c.product.c_str(),
                        c.serial.c_str());
                    out.push_back(std::move(c));
                }
            }
        }

        if (handle) {
            libusb_close(handle);
        }
        libusb_free_config_descriptor(cfg);
    }

    libusb_free_device_list(list, 1);
    DBG("UsbEnumerator::EnumerateStillImaging found=%zu", out.size());
    return Result<std::vector<UsbDeviceCandidate>>::Success(std::move(out));
}

} // namespace proto
