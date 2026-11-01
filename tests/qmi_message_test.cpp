#include "j5qmi/qmi_message.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    using j5qmi::Tlv;

    const std::vector<Tlv> tlvs = {
        {0x01, {0x02, 0x03}},
        {0x10, {0xAA, 0xBB, 0xCC}}
    };

    // NAS uses a 16-bit transaction identifier.
    const auto encoded = j5qmi::encode_qmi_message(
        0x03, 0x00, 0x1234, 0x0024, tlvs);
    assert(!encoded.empty());

    const auto parsed = j5qmi::parse_qmi_message(
        0x03, encoded.data(), encoded.size());
    assert(parsed);
    assert(parsed.message.transaction_id == 0x1234);
    assert(parsed.message.message_id == 0x0024);
    assert(parsed.message.tlvs.size() == 2U);

    const auto* found = j5qmi::find_tlv(parsed.message, 0x10);
    assert(found != nullptr);
    assert(found->value == std::vector<uint8_t>({0xAA, 0xBB, 0xCC}));

    // CTL uses an 8-bit transaction identifier.
    const auto ctl = j5qmi::encode_qmi_message(
        0x00, 0x00, 0x42, 0x0021, {});
    assert(ctl.size() == 5U);

    const auto ctl_parsed = j5qmi::parse_qmi_message(
        0x00, ctl.data(), ctl.size());
    assert(ctl_parsed);
    assert(ctl_parsed.message.transaction_id == 0x42);
    assert(j5qmi::encode_qmi_message(
        0x00, 0x00, 0x100, 0x0021, {}).empty());

    // Standard QMI result TLV: SUCCESS (0), error (0).
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

    const std::vector<uint8_t> malformed = {
        0x02, 0x01, 0x00, 0x24, 0x00, 0x04, 0x00,
        0x10, 0x05, 0x00
    };
    const auto bad = j5qmi::parse_qmi_message(
        0x03, malformed.data(), malformed.size());
    assert(!bad);
    assert(bad.error == j5qmi::MessageError::MalformedTlv);

    std::cout << "QMI message/TLV tests passed\n";
    return 0;
}
