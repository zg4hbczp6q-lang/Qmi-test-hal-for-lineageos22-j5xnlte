
#include "j5qmi/qmi_message.h"

#include <utility>

namespace j5qmi {
namespace {

uint16_t read_le16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(
               static_cast<uint16_t>(data[1]) << 8);
}

void append_le16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value & 0xFFU));
    out->push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
}

// QMI CTL uses an 8-bit transaction ID.
// Other QMI services use 16-bit transaction IDs.
size_t transaction_width(uint8_t service) {
    return service == 0x00 ? 1U : 2U;
}

}  // namespace

std::vector<uint8_t> encode_qmi_message(
    uint8_t service,
    uint8_t control_flags,
    uint16_t transaction_id,
    uint16_t message_id,
    const std::vector<Tlv>& tlvs) {

    const size_t txn_width = transaction_width(service);

    if (txn_width == 1U && transaction_id > 0xFFU) {
        return {};
    }

    // The QMI TLV stream length is stored in a 16-bit field.
    size_t tlv_bytes = 0;

    for (const auto& tlv : tlvs) {
        // Each TLV requires 1 byte type + 2 bytes length.
        // Therefore its value cannot exceed 65532 bytes.
        if (tlv.value.size() > 0xFFFFU - 3U) {
            return {};
        }

        const size_t encoded_size = 3U + tlv.value.size();

        if (tlv_bytes > 0xFFFFU - encoded_size) {
            return {};
        }

        tlv_bytes += encoded_size;
    }

    std::vector<uint8_t> out;
    out.reserve(1U + txn_width + 4U + tlv_bytes);

    // Control flags.
    out.push_back(control_flags);

    // Transaction ID.
    out.push_back(static_cast<uint8_t>(transaction_id & 0xFFU));

    if (txn_width == 2U) {
        out.push_back(
            static_cast<uint8_t>((transaction_id >> 8) & 0xFFU));
    }

    // Message ID and total TLV length.
    append_le16(&out, message_id);
    append_le16(&out, static_cast<uint16_t>(tlv_bytes));

    // TLV data.
    for (const auto& tlv : tlvs) {
        out.push_back(tlv.type);
        append_le16(
            &out,
            static_cast<uint16_t>(tlv.value.size()));

        out.insert(
            out.end(),
            tlv.value.begin(),
            tlv.value.end());
    }

    return out;
}

MessageResult parse_qmi_message(
    uint8_t service,
    const uint8_t* data,
    size_t size) {

    MessageResult result{};

    const size_t txn_width = transaction_width(service);
    const size_t header_size = 1U + txn_width + 4U;

    if (data == nullptr || size < header_size) {
        result.error = MessageError::TooShort;
        return result;
    }

    size_t offset = 0;

    result.message.control_flags = data[offset++];

    // Transaction ID.
    result.message.transaction_id = data[offset++];

    if (txn_width == 2U) {
        result.message.transaction_id |=
            static_cast<uint16_t>(
                static_cast<uint16_t>(data[offset++]) << 8);
    }

    // Message ID.
    result.message.message_id = read_le16(data + offset);
    offset += 2U;

    // Total TLV stream length.
    const size_t tlv_length = read_le16(data + offset);
    offset += 2U;

    if (tlv_length != size - offset) {
        result.error = MessageError::LengthMismatch;
        return result;
    }

    // Parse TLVs.
    while (offset < size) {
        if (size - offset < 3U) {
            result.error = MessageError::MalformedTlv;
            return result;
        }

        Tlv tlv{};

        tlv.type = data[offset++];

        const size_t length = read_le16(data + offset);
        offset += 2U;

        if (length > size - offset) {
            result.error = MessageError::MalformedTlv;
            return result;
        }

        tlv.value.assign(
            data + offset,
            data + offset + length);

        offset += length;

        result.message.tlvs.push_back(std::move(tlv));
    }

    return result;
}

const Tlv* find_tlv(
    const QmiMessage& message,
    uint8_t type) {

    for (const auto& tlv : message.tlvs) {
        if (tlv.type == type) {
            return &tlv;
        }
    }

    return nullptr;
}

bool decode_result_tlv(
    const QmiMessage& message,
    uint16_t* result,
    uint16_t* error) {

    const Tlv* tlv = find_tlv(message, 0x02);

    if (tlv == nullptr ||
        tlv->value.size() != 4U ||
        result == nullptr ||
        error == nullptr) {
        return false;
    }

    *result = read_le16(tlv->value.data());
    *error = read_le16(tlv->value.data() + 2U);

    return true;
}

}  // namespace j5qmi
