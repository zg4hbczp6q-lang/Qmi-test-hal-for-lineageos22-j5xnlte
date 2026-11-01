#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace j5qmi {

// QMUX framing used by the MSM8916 WWAN control port.
// This parser deliberately does not send commands to the modem.
struct QmuxFrame {
    uint8_t flags{};
    uint8_t service{};
    uint8_t client_id{};
    std::vector<uint8_t> qmi_message;
};

enum class ParseError {
    None,
    TooShort,
    BadMarker,
    LengthMismatch,
    EmptyQmiMessage
};

struct ParseResult {
    ParseError error{ParseError::None};
    QmuxFrame frame{};
    explicit operator bool() const { return error == ParseError::None; }
};

ParseResult parse_qmux(const uint8_t* data, size_t size);
std::vector<uint8_t> encode_qmux(uint8_t flags,
                                 uint8_t service,
                                 uint8_t client_id,
                                 const std::vector<uint8_t>& qmi_message);
const char* parse_error_string(ParseError error);

}  // namespace j5qmi
