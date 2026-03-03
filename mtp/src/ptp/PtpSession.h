#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../core/Types.h"
#include "../usb/UsbTransport.h"
#include "PtpContainers.h"

namespace proto {

class PtpSession {
public:
    explicit PtpSession(UsbTransport& transport);

    Status OpenSession();
    Status CloseSession();

    Result<ptp::Response> Transaction(uint16_t opcode,
                                      const std::vector<uint32_t>& params,
                                      std::vector<uint8_t>* data_in,
                                      const std::vector<uint8_t>* data_out);
    Result<ptp::Response> TransactionWithRetry(uint16_t opcode,
                                               const std::vector<uint32_t>& params,
                                               std::vector<uint8_t>* data_in,
                                               const std::vector<uint8_t>* data_out,
                                               int attempts);

    Result<std::vector<uint8_t>> GetData(uint16_t opcode, const std::vector<uint32_t>& params);
    Status SendData(uint16_t opcode, const std::vector<uint32_t>& params, const std::vector<uint8_t>& payload);

    uint32_t SessionId() const { return _session_id; }

private:
    Status ReopenSessionAfterDesync(uint16_t triggering_opcode);

    Status SendCommand(uint16_t opcode, const std::vector<uint32_t>& params, uint32_t txid);
    Status SendDataPhase(uint16_t opcode, uint32_t txid, const std::vector<uint8_t>& payload);
    Result<std::vector<uint8_t>> ReadDataPhase(uint16_t expected_opcode, uint32_t expected_txid);
    Result<ptp::Response> ReadResponse(uint32_t expected_txid);

    static void AppendU16(std::vector<uint8_t>& out, uint16_t v);
    static void AppendU32(std::vector<uint8_t>& out, uint32_t v);
    static uint16_t ReadU16(const uint8_t* p);
    static uint32_t ReadU32(const uint8_t* p);

    UsbTransport& _transport;
    std::optional<ptp::Response> _deferred_response;
    bool _session_open = false;
    uint32_t _session_id = 1;
    uint32_t _next_txid = 1;
};

} // namespace proto
