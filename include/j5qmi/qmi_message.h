#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace j5qmi {

struct Tlv {
    uint8_t type{};
    std::vector<uint8_t> value;
};

enum class MessageError {
    None,
    TooShort,
    LengthMismatch,
    MalformedTlv
};

struct QmiMessage {
    uint8_t control_flags{};
    uint16_t transaction_id{};
    uint16_t message_id{};
    std::vector<Tlv> tlvs;
};

struct MessageResult {
    MessageError error{MessageError::None};
    QmiMessage message{};
    explicit operator bool() const { return error == MessageError::None; }
};

std::vector<uint8_t> encode_qmi_message(
    uint8_t service, uint8_t control_flags, uint16_t transaction_id,
    uint16_t message_id, const std::vector<Tlv>& tlvs);

MessageResult parse_qmi_message(uint8_t service,
                                const uint8_t* data, size_t size);

const Tlv* find_tlv(const QmiMessage& message, uint8_t type);

bool decode_result_tlv(const QmiMessage& message,
                       uint16_t* result, uint16_t* error);

}  // namespace j5qmi
