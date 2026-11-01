
#include "j5qmi/qmi_message.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    using j5qmi::Tlv;

    // NAS uses a 16-bit transaction identifier.
    const std::vector<Tlv> tlvs = {
        {0x01, {0x02, 0x03}},
        {0x10, {0xAA, 0xBB, 0xCC}}
    };

    const auto encoded = j5qmi::encode_qmi_message(
        0x03, 0x00, 0x1234, 0x0024, tlvs);

    assert(!encoded.empty());

    const auto parsed = j5qmi::parse_qmi_message(
        0x03, encoded.data(), encoded.size());

    assert(parsed);
    assert(parsed.message.transaction_id == 0x1234);
    assert(parsed.message.message_id == 0x0024);
    assert(parsed.message.tlvs.size() == 2U);

    const auto* found = j5qmi::find_tlv(
        parsed.message, 0x10);

    assert(found != nullptr);
    assert(found->value ==
           std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));

    // CTL uses an 8-bit transaction identifier.
    const auto ctl = j5qmi::encode_qmi_message(
        0x00, 0x00, 0x42, 0x0021, {});

    // CTL header: flags(1) + transaction ID(1)
    // + message ID(2) + TLV length(2).
    assert(ctl.size() == 6U);

    const auto ctl_parsed = j5qmi::parse_qmi_message(
        0x00, ctl.data(), ctl.size());

    assert(ctl_parsed);
    assert(ctl_parsed.message.transaction_id == 0x42);

    // CTL transaction ID must fit into 8 bits.
    assert(j5qmi::encode_qmi_message(
        0x00, 0x00, 0x100, 0x0021, {}).empty());

    // Standard QMI result TLV: result = SUCCESS, error = 0.
    const std::vector<Tlv> result_tlv = {
        {0x02, {0x00, 0x00, 0x00, 0x00}}
    };

    const auto response = j5qmi::encode_qmi_message(
        0x03, 0x02, 7, 0x0024, result_tlv);

    const auto response_parsed = j5qmi::parse_qmi_message(
        0x03, response.data(), response.size());

    uint16_t result = 0xFFFF;
    uint16_t error = 0xFFFF;

    assert(response_parsed);
    assert(j5qmi::decode_result_tlv(
        response_parsed.message, &result, &error));

    assert(result == 0U);
    assert(error == 0U);

    // Malformed TLV:
    // Total TLV length is 3 bytes, but the TLV declares
    // 5 bytes of value while no value bytes are present.
    const std::vector<uint8_t> malformed = {
        0x02, 0x01, 0x00, 0x24, 0x00,
        0x03, 0x00,
        0x10, 0x05, 0x00
    };

    const auto bad = j5qmi::parse_qmi_message(
        0x03, malformed.data(), malformed.size());

    assert(!bad);
    assert(bad.error == j5qmi::MessageError::MalformedTlv);

    // Reject a packet with an incorrect total TLV length.
    const std::vector<uint8_t> wrong_total_length = {
        0x02, 0x01, 0x00, 0x24, 0x00,
        0x04, 0x00,
        0x10, 0x01, 0x00
    };

    const auto bad_length = j5qmi::parse_qmi_message(
        0x03,
        wrong_total_length.data(),
        wrong_total_length.size());

    assert(!bad_length);
    assert(bad_length.error == j5qmi::MessageError::LengthMismatch);

    // Reject a packet shorter than the QMI header.
    const std::vector<uint8_t> too_short = {
        0x00, 0x01, 0x00
    };

    const auto short_result = j5qmi::parse_qmi_message(
        0x03, too_short.data(), too_short.size());

    assert(!short_result);
    assert(short_result.error == j5qmi::MessageError::TooShort);

    // Reject null input.
    const auto null_result = j5qmi::parse_qmi_message(
        0x03, nullptr, 0);

    assert(!null_result);
    assert(null_result.error == j5qmi::MessageError::TooShort);

    // Maximum valid TLV value:
    // 65532 bytes + 3 bytes TLV header = 65535 bytes.
    const std::vector<Tlv> max_tlv = {
        {0x10, std::vector<uint8_t>(65532U, 0xAA)}
    };

    const auto max_encoded = j5qmi::encode_qmi_message(
        0x03, 0x00, 1, 0x0024, max_tlv);

    assert(!max_encoded.empty());
    assert(max_encoded.size() == 7U + 65535U);

    const auto max_parsed = j5qmi::parse_qmi_message(
        0x03, max_encoded.data(), max_encoded.size());

    assert(max_parsed);
    assert(max_parsed.message.tlvs.size() == 1U);
    assert(max_parsed.message.tlvs[0].value.size() == 65532U);

    // A single TLV value of 65533 bytes cannot fit.
    const std::vector<Tlv> oversized_tlv = {
        {0x10, std::vector<uint8_t>(65533U, 0xAA)}
    };

    assert(j5qmi::encode_qmi_message(
        0x03, 0x00, 1, 0x0024, oversized_tlv).empty());

    // Two individually valid TLVs can still exceed
    // the maximum total TLV stream length.
    const std::vector<Tlv> overflowing_tlvs = {
        {0x10, std::vector<uint8_t>(65532U, 0xAA)},
        {0x11, {}}
    };

    assert(j5qmi::encode_qmi_message(
        0x03, 0x00, 1, 0x0024, overflowing_tlvs).empty());

    // Result TLV decoder must reject null output pointers.
    assert(!j5qmi::decode_result_tlv(
        response_parsed.message, nullptr, &error));

    assert(!j5qmi::decode_result_tlv(
        response_parsed.message, &result, nullptr));

    std::cout << "QMI message/TLV tests passed\n";
    return 0;
}
