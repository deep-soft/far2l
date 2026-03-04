#include "PtpSession.h"
#include "../MTPLog.h"

#include <algorithm>
#include <cstring>

namespace proto {

namespace {
constexpr int kCmdTimeoutMs = 5000;
constexpr size_t kHeaderSize = sizeof(ptp::ContainerHeader);
constexpr uint16_t kRespSessionAlreadyOpen = 0x201E;

ErrorCode MapPtpResponse(uint16_t code) {
    if (code == ptp::kRespOk) return ErrorCode::Ok;
    if (code == ptp::kRespDeviceBusy) return ErrorCode::Busy;
    if (code == ptp::kRespOperationNotSupported) return ErrorCode::Unsupported;
    if (code == ptp::kRespSessionNotOpen) return ErrorCode::Disconnected;
    if (code == kRespSessionAlreadyOpen) return ErrorCode::Busy;
    return ErrorCode::Io;
}

std::string PtpResponseToString(uint16_t code) {
    switch (code) {
        case ptp::kRespOk: return "OK";
        case ptp::kRespDeviceBusy: return "DeviceBusy";
        case ptp::kRespOperationNotSupported: return "OperationNotSupported";
        case ptp::kRespSessionNotOpen: return "SessionNotOpen";
        case kRespSessionAlreadyOpen: return "SessionAlreadyOpen";
        default: return "USB transport error " + std::to_string(code);
    }
}
}

PtpSession::PtpSession(UsbTransport& transport)
    : _transport(transport) {
}

Status PtpSession::OpenSession() {
    if (_session_open) {
        return OkStatus();
    }
    _next_txid = 1;
    auto res = Transaction(ptp::kOpOpenSession, {_session_id}, nullptr, nullptr);
    if (!res.ok && !(res.code == ErrorCode::Busy && res.message == "SessionAlreadyOpen")) {
        return Status::Failure(res.code, res.message, res.retryable);
    }
    _session_open = true;
    return OkStatus();
}

Status PtpSession::CloseSession() {
    if (!_session_open) {
        return OkStatus();
    }
    auto res = Transaction(ptp::kOpCloseSession, {}, nullptr, nullptr);
    _session_open = false;
    if (!res.ok) {
        return Status::Failure(res.code, res.message, res.retryable);
    }
    return OkStatus();
}

Result<ptp::Response> PtpSession::Transaction(uint16_t opcode,
                                              const std::vector<uint32_t>& params,
                                              std::vector<uint8_t>* data_in,
                                              const std::vector<uint8_t>* data_out) {
    _deferred_response.reset();
    const uint32_t txid = _next_txid++;

    Status cmd = SendCommand(opcode, params, txid);
    if (!cmd.ok) {
        return Result<ptp::Response>::Failure(cmd.code, cmd.message, cmd.retryable);
    }

    if (data_out && !data_out->empty()) {
        Status d = SendDataPhase(opcode, txid, *data_out);
        if (!d.ok) {
            return Result<ptp::Response>::Failure(d.code, d.message, d.retryable);
        }
    }

    if (data_in) {
        auto d = ReadDataPhase(opcode, txid);
        if (!d.ok) {
            return Result<ptp::Response>::Failure(d.code, d.message, d.retryable);
        }
        *data_in = std::move(d.value);
        if (_deferred_response.has_value()) {
            auto deferred = std::move(*_deferred_response);
            _deferred_response.reset();
            if (deferred.code != ptp::kRespOk) {
                return Result<ptp::Response>::Failure(MapPtpResponse(deferred.code),
                                                      PtpResponseToString(deferred.code),
                                                      deferred.code == ptp::kRespDeviceBusy);
            }
            auto out = Result<ptp::Response>::Success(std::move(deferred));
            return out;
        }
    }

    return ReadResponse(txid);
}

Result<ptp::Response> PtpSession::TransactionWithRetry(uint16_t opcode,
                                                       const std::vector<uint32_t>& params,
                                                       std::vector<uint8_t>* data_in,
                                                       const std::vector<uint8_t>* data_out,
                                                       int attempts) {
    Result<ptp::Response> last = Result<ptp::Response>::Failure(ErrorCode::Io, "Transaction not started");
    for (int i = 0; i < attempts; ++i) {
        std::vector<uint8_t> localData;
        std::vector<uint8_t>* dataPtr = data_in ? &localData : nullptr;
        last = Transaction(opcode, params, dataPtr, data_out);
        if (last.ok) {
            if (data_in && dataPtr) {
                *data_in = std::move(localData);
            }
            return last;
        }
        const bool canRecoverDesync = (last.code == ErrorCode::ProtocolDesync);
        if (!last.retryable && !canRecoverDesync) {
            return last;
        }
        if (last.code == ErrorCode::Io || last.code == ErrorCode::Timeout) {
            (void)_transport.RecoverStall();
        } else if (last.code == ErrorCode::ProtocolDesync) {
            (void)_transport.RecoverStall();
            auto rs = ReopenSessionAfterDesync(opcode);
            DBG("PTP desync recovery opcode=0x%04x status_ok=%d msg=%s",
                opcode,
                rs.ok ? 1 : 0,
                rs.message.c_str());
        }
    }
    return last;
}

Result<std::vector<uint8_t>> PtpSession::GetData(uint16_t opcode, const std::vector<uint32_t>& params) {
    std::vector<uint8_t> payload;
    auto r = TransactionWithRetry(opcode, params, &payload, nullptr, 3);
    if (!r.ok) {
        return Result<std::vector<uint8_t>>::Failure(r.code, r.message, r.retryable);
    }
    return Result<std::vector<uint8_t>>::Success(std::move(payload));
}

Status PtpSession::SendData(uint16_t opcode, const std::vector<uint32_t>& params, const std::vector<uint8_t>& payload) {
    auto r = Transaction(opcode, params, nullptr, &payload);
    if (!r.ok) {
        return Status::Failure(r.code, r.message, r.retryable);
    }
    return OkStatus();
}

Status PtpSession::SendCommand(uint16_t opcode, const std::vector<uint32_t>& params, uint32_t txid) {
    std::vector<uint8_t> pkt;
    pkt.reserve(kHeaderSize + params.size() * sizeof(uint32_t));

    AppendU32(pkt, static_cast<uint32_t>(kHeaderSize + params.size() * sizeof(uint32_t)));
    AppendU16(pkt, ptp::kContainerCommand);
    AppendU16(pkt, opcode);
    AppendU32(pkt, txid);
    for (uint32_t p : params) {
        AppendU32(pkt, p);
    }

    return _transport.BulkOut(pkt.data(), pkt.size(), kCmdTimeoutMs);
}

Status PtpSession::SendDataPhase(uint16_t opcode, uint32_t txid, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> pkt;
    pkt.reserve(kHeaderSize + payload.size());

    AppendU32(pkt, static_cast<uint32_t>(kHeaderSize + payload.size()));
    AppendU16(pkt, ptp::kContainerData);
    AppendU16(pkt, opcode);
    AppendU32(pkt, txid);
    pkt.insert(pkt.end(), payload.begin(), payload.end());

    return _transport.BulkOut(pkt.data(), pkt.size(), 15000);
}

Result<std::vector<uint8_t>> PtpSession::ReadDataPhase(uint16_t expected_opcode, uint32_t expected_txid) {
    auto first = _transport.BulkIn(64 * 1024, kCmdTimeoutMs);
    if (!first.ok) {
        return Result<std::vector<uint8_t>>::Failure(first.code, first.message, first.retryable);
    }
    if (first.value.size() < kHeaderSize) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::ProtocolDesync, "Short PTP data header");
    }

    const uint8_t* h = first.value.data();
    const uint32_t len = ReadU32(h + 0);
    const uint16_t type = ReadU16(h + 4);
    const uint16_t code = ReadU16(h + 6);
    const uint32_t txid = ReadU32(h + 8);

    DBG("PTP ReadDataPhase op=0x%04x exp_txid=%u got_type=%u got_code=0x%04x got_txid=%u len=%u",
        expected_opcode, expected_txid, type, code, txid, len);

    if (type == ptp::kContainerResponse && txid == expected_txid) {
        ptp::Response resp;
        resp.code = code;
        resp.txid = txid;
        _deferred_response = resp;
        return Result<std::vector<uint8_t>>::Success(std::vector<uint8_t>{});
    }

    if (type != ptp::kContainerData || code != expected_opcode || txid != expected_txid || len < kHeaderSize) {
        return Result<std::vector<uint8_t>>::Failure(ErrorCode::ProtocolDesync, "PTP data phase mismatch");
    }

    const size_t dataLen = len - kHeaderSize;
    std::vector<uint8_t> data;
    data.reserve(dataLen);
    if (first.value.size() > kHeaderSize) {
        const size_t initial = std::min(dataLen, first.value.size() - kHeaderSize);
        data.insert(data.end(), first.value.begin() + kHeaderSize, first.value.begin() + kHeaderSize + initial);
    }
    while (data.size() < dataLen) {
        const size_t remain = dataLen - data.size();
        const size_t chunk = std::min<size_t>(remain, 64 * 1024);
        auto part = _transport.BulkIn(chunk, 15000);
        if (!part.ok) {
            return Result<std::vector<uint8_t>>::Failure(part.code, part.message, part.retryable);
        }
        if (part.value.empty()) {
            return Result<std::vector<uint8_t>>::Failure(ErrorCode::ProtocolDesync, "PTP data phase short read");
        }
        data.insert(data.end(), part.value.begin(), part.value.end());
    }

    if (data.size() > dataLen) {
        data.resize(dataLen);
    }

    return Result<std::vector<uint8_t>>::Success(std::move(data));
}

Result<ptp::Response> PtpSession::ReadResponse(uint32_t expected_txid) {
    auto first = _transport.BulkIn(4 * 1024, kCmdTimeoutMs);
    if (!first.ok) {
        return Result<ptp::Response>::Failure(first.code, first.message, first.retryable);
    }
    if (first.value.size() < kHeaderSize) {
        return Result<ptp::Response>::Failure(ErrorCode::ProtocolDesync, "Short PTP response header");
    }

    const uint8_t* h = first.value.data();
    const uint32_t len = ReadU32(h + 0);
    const uint16_t type = ReadU16(h + 4);
    const uint16_t code = ReadU16(h + 6);
    const uint32_t txid = ReadU32(h + 8);

    DBG("PTP ReadResponse exp_txid=%u got_type=%u got_code=0x%04x got_txid=%u len=%u",
        expected_txid, type, code, txid, len);

    if (type != ptp::kContainerResponse || txid != expected_txid || len < kHeaderSize) {
        return Result<ptp::Response>::Failure(ErrorCode::ProtocolDesync, "PTP response mismatch");
    }

    std::vector<uint8_t> tail;
    const size_t tailLen = len - kHeaderSize;
    if (first.value.size() > kHeaderSize) {
        const size_t initial = std::min(tailLen, first.value.size() - kHeaderSize);
        tail.insert(tail.end(), first.value.begin() + kHeaderSize, first.value.begin() + kHeaderSize + initial);
    }
    while (tail.size() < tailLen) {
        auto part = _transport.BulkIn(std::min<size_t>(tailLen - tail.size(), 1024), kCmdTimeoutMs);
        if (!part.ok) {
            return Result<ptp::Response>::Failure(part.code, part.message, part.retryable);
        }
        if (part.value.empty()) {
            return Result<ptp::Response>::Failure(ErrorCode::ProtocolDesync, "PTP response short read");
        }
        tail.insert(tail.end(), part.value.begin(), part.value.end());
    }

    ptp::Response resp;
    resp.code = code;
    resp.txid = txid;
    for (size_t off = 0; off + 4 <= tail.size(); off += 4) {
        resp.params.push_back(ReadU32(tail.data() + off));
    }

    if (resp.code != ptp::kRespOk) {
        return Result<ptp::Response>::Failure(MapPtpResponse(resp.code), PtpResponseToString(resp.code), resp.code == ptp::kRespDeviceBusy);
    }

    return Result<ptp::Response>::Success(std::move(resp));
}

Status PtpSession::ReopenSessionAfterDesync(uint16_t triggering_opcode) {
    if (!_session_open) {
        return OkStatus();
    }
    if (triggering_opcode == ptp::kOpOpenSession || triggering_opcode == ptp::kOpCloseSession) {
        _session_open = false;
        ++_session_id;
        _next_txid = 1;
        return OkStatus();
    }

    _session_open = false;
    ++_session_id;
    _next_txid = 1;

    auto reopen = Transaction(ptp::kOpOpenSession, {_session_id}, nullptr, nullptr);
    if (!reopen.ok) {
        return Status::Failure(reopen.code, reopen.message, reopen.retryable);
    }
    _session_open = true;
    return OkStatus();
}

void PtpSession::AppendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void PtpSession::AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

uint16_t PtpSession::ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t PtpSession::ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace proto
