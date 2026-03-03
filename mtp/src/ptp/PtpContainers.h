#pragma once

#include <cstdint>
#include <vector>

namespace proto {
namespace ptp {

constexpr uint16_t kContainerCommand = 1;
constexpr uint16_t kContainerData = 2;
constexpr uint16_t kContainerResponse = 3;
constexpr uint16_t kContainerEvent = 4;

constexpr uint16_t kOpGetDeviceInfo = 0x1001;
constexpr uint16_t kOpOpenSession = 0x1002;
constexpr uint16_t kOpCloseSession = 0x1003;
constexpr uint16_t kOpGetStorageIDs = 0x1004;
constexpr uint16_t kOpGetStorageInfo = 0x1005;
constexpr uint16_t kOpGetNumObjects = 0x1006;
constexpr uint16_t kOpGetObjectHandles = 0x1007;
constexpr uint16_t kOpGetObjectInfo = 0x1008;
constexpr uint16_t kOpGetObject = 0x1009;
constexpr uint16_t kOpDeleteObject = 0x100B;
constexpr uint16_t kOpSendObjectInfo = 0x100C;
constexpr uint16_t kOpSendObject = 0x100D;
constexpr uint16_t kOpMoveObject = 0x1019;
constexpr uint16_t kOpCopyObject = 0x101A;
constexpr uint16_t kOpGetPartialObject = 0x101B;

constexpr uint16_t kOpGetObjectPropsSupported = 0x9801;
constexpr uint16_t kOpGetObjectPropDesc = 0x9802;
constexpr uint16_t kOpGetObjectPropValue = 0x9803;
constexpr uint16_t kOpSetObjectPropValue = 0x9804;
constexpr uint16_t kOpGetObjectPropList = 0x9805;
constexpr uint16_t kOpSetObjectPropList = 0x9806;

constexpr uint16_t kRespOk = 0x2001;
constexpr uint16_t kRespGeneralError = 0x2002;
constexpr uint16_t kRespSessionNotOpen = 0x2003;
constexpr uint16_t kRespInvalidTransactionId = 0x2004;
constexpr uint16_t kRespOperationNotSupported = 0x2005;
constexpr uint16_t kRespParameterNotSupported = 0x2006;
constexpr uint16_t kRespIncompleteTransfer = 0x2007;
constexpr uint16_t kRespDeviceBusy = 0x2019;
constexpr uint16_t kRespInvalidParentObject = 0x201A;

#pragma pack(push, 1)
struct ContainerHeader {
    uint32_t length = 0;
    uint16_t type = 0;
    uint16_t code = 0;
    uint32_t transaction_id = 0;
};
#pragma pack(pop)

struct Response {
    uint16_t code = 0;
    uint32_t txid = 0;
    std::vector<uint32_t> params;
};

} // namespace ptp
} // namespace proto
