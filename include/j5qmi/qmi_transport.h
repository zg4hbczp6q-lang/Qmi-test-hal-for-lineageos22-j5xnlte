#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace j5qmi {

enum class TransportError {
    None,
    InvalidArgument,
    IoError,
    Timeout,
    PeerClosed,
    BadMarker,
    FrameTooLarge,
    InvalidFrame,
    UnexpectedFrame,
};

struct TransportResult {
    TransportError error{TransportError::None};
    std::vector<uint8_t> frame;
    int system_error{0};
    explicit operator bool() const { return error == TransportError::None; }
};

// Synchronous, serialized QMUX transport over an already-open file descriptor.
// The caller owns fd and must keep it open for the lifetime of this object.
// This class does not allocate QMI clients or issue modem configuration commands.
class QmiTransport {
public:
    explicit QmiTransport(int fd, size_t max_frame_size = 65535);

    TransportResult transact(const std::vector<uint8_t>& request_frame,
                             uint8_t expected_service,
                             uint8_t expected_client,
                             uint16_t expected_transaction_id,
                             int timeout_ms);

    static const char* error_string(TransportError error);

private:
    bool write_all(const uint8_t* data, size_t size, int timeout_ms,
                   int* system_error, TransportError* error);
    bool read_exact(uint8_t* data, size_t size, int timeout_ms,
                    int* system_error, TransportError* error);

    int fd_;
    size_t max_frame_size_;
    std::mutex mutex_;
};

}  // namespace j5qmi
