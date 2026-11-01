#include "j5qmi/qmi_frame.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    // Synthetic QMI CTL request payload; this is not transmitted to a modem.
    const std::vector<uint8_t> qmi = {
        0x00, 0x01, 0x21, 0x00, 0x00, 0x00
    };

    const auto encoded = j5qmi::encode_qmux(0x00, 0x00, 0x00, qmi);
    assert(!encoded.empty());

    const auto parsed = j5qmi::parse_qmux(encoded.data(), encoded.size());
    assert(parsed);
    assert(parsed.frame.flags == 0x00);
    assert(parsed.frame.service == 0x00);
    assert(parsed.frame.client_id == 0x00);
    assert(parsed.frame.qmi_message == qmi);

    auto malformed = encoded;
    malformed[1] = 0xFF;
    const auto bad = j5qmi::parse_qmux(malformed.data(), malformed.size());
    assert(!bad);
    assert(bad.error == j5qmi::ParseError::LengthMismatch);

    const std::vector<uint8_t> bad_marker = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto marker_result = j5qmi::parse_qmux(bad_marker.data(), bad_marker.size());
    assert(!marker_result);
    assert(marker_result.error == j5qmi::ParseError::BadMarker);

    std::cout << "QMI QMUX framing tests passed\n";
    return 0;
}
