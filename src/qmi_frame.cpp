#include "j5qmi/qmi_frame.h"

namespace j5qmi {

ParseResult parse_qmux(const uint8_t* data, size_t size) {
    ParseResult result{};
    if (data == nullptr || size < 7) {
        result.error = ParseError::TooShort;
        return result;
    }
    if (data[0] != 0x01) {
        result.error = ParseError::BadMarker;
        return result;
    }

    const size_t declared_after_length =
        static_cast<size_t>(data[1]) |
        (static_cast<size_t>(data[2]) << 8);

    // QMUX length counts bytes after the 3-byte marker/length prefix.
    if (declared_after_length != size - 3) {
        result.error = ParseError::LengthMismatch;
        return result;
    }
    if (size <= 6) {
        result.error = ParseError::EmptyQmiMessage;
        return result;
    }

    result.frame.flags = data[3];
    result.frame.service = data[4];
    result.frame.client_id = data[5];
    result.frame.qmi_message.assign(data + 6, data + size);
    return result;
}

std::vector<uint8_t> encode_qmux(uint8_t flags,
                                uint8_t service,
                                uint8_t client_id,
                                const std::vector<uint8_t>& qmi_message) {
    if (qmi_message.empty() || qmi_message.size() > 0xFFFF - 3) {
        return {};
    }

    const size_t length = 3 + qmi_message.size();
    std::vector<uint8_t> out;
    out.reserve(3 + length);
    out.push_back(0x01);
    out.push_back(static_cast<uint8_t>(length & 0xFF));
    out.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    out.push_back(flags);
    out.push_back(service);
    out.push_back(client_id);
    out.insert(out.end(), qmi_message.begin(), qmi_message.end());
    return out;
}

const char* parse_error_string(ParseError error) {
    switch (error) {
        case ParseError::None: return "none";
        case ParseError::TooShort: return "too short";
        case ParseError::BadMarker: return "invalid QMUX marker";
        case ParseError::LengthMismatch: return "QMUX length mismatch";
        case ParseError::EmptyQmiMessage: return "empty QMI message";
    }
    return "unknown";
}

}  // namespace j5qmi
