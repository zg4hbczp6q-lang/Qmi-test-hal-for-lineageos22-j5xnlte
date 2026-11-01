#include "j5qmi/qmi_transport.h"

#include "j5qmi/qmi_message.h"

#include <cerrno>
#include <climits>
#include <poll.h>
#include <unistd.h>

namespace j5qmi {
namespace {

uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

bool wait_fd(int fd, short events, int timeout_ms, int* system_error,
             TransportError* error) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    for (;;) {
        const int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0) {
            if ((pfd.revents & (POLLERR | POLLNVAL)) != 0) {
                *error = TransportError::IoError;
                *system_error = EIO;
                return false;
            }
            if ((pfd.revents & POLLHUP) != 0 &&
                (pfd.revents & events) == 0) {
                *error = TransportError::PeerClosed;
                *system_error = 0;
                return false;
            }
            if ((pfd.revents & events) != 0) return true;
            continue;
        }
        if (rc == 0) {
            *error = TransportError::Timeout;
            *system_error = 0;
            return false;
        }
        if (errno == EINTR) continue;
        *error = TransportError::IoError;
        *system_error = errno;
        return false;
    }
}

}  // namespace

QmiTransport::QmiTransport(int fd, size_t max_frame_size)
    : fd_(fd), max_frame_size_(max_frame_size) {}

bool QmiTransport::write_all(const uint8_t* data, size_t size, int timeout_ms,
                             int* system_error, TransportError* error) {
    size_t offset = 0;
    while (offset < size) {
        if (!wait_fd(fd_, POLLOUT, timeout_ms, system_error, error)) return false;
        const ssize_t n = write(fd_, data + offset, size - offset);
        if (n > 0) {
            offset += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        *error = n == 0 ? TransportError::PeerClosed : TransportError::IoError;
        *system_error = n == 0 ? 0 : errno;
        return false;
    }
    return true;
}

bool QmiTransport::read_exact(uint8_t* data, size_t size, int timeout_ms,
                              int* system_error, TransportError* error) {
    size_t offset = 0;
    while (offset < size) {
        if (!wait_fd(fd_, POLLIN, timeout_ms, system_error, error)) return false;
        const ssize_t n = read(fd_, data + offset, size - offset);
        if (n > 0) {
            offset += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            *error = TransportError::PeerClosed;
            *system_error = 0;
            return false;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
        *error = TransportError::IoError;
        *system_error = errno;
        return false;
    }
    return true;
}

TransportResult QmiTransport::transact(
    const std::vector<uint8_t>& request_frame, uint8_t expected_service,
    uint8_t expected_client, uint16_t expected_transaction_id, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    TransportResult result{};
    if (fd_ < 0 || timeout_ms < 0 || request_frame.size() < 7 ||
        request_frame.size() > max_frame_size_ || request_frame[0] != 0x01 ||
        static_cast<size_t>(read_le16(request_frame.data() + 1)) !=
            request_frame.size() - 3) {
        result.error = TransportError::InvalidArgument;
        return result;
    }

    if (!write_all(request_frame.data(), request_frame.size(), timeout_ms,
                   &result.system_error, &result.error)) return result;

    uint8_t prefix[3]{};
    if (!read_exact(prefix, sizeof(prefix), timeout_ms,
                    &result.system_error, &result.error)) return result;
    if (prefix[0] != 0x01) {
        result.error = TransportError::BadMarker;
        return result;
    }

    const size_t body_length = read_le16(prefix + 1);
    const size_t total_length = body_length + 3;
    if (body_length < 4 || total_length > max_frame_size_) {
        result.error = TransportError::FrameTooLarge;
        return result;
    }

    result.frame.resize(total_length);
    result.frame[0] = prefix[0];
    result.frame[1] = prefix[1];
    result.frame[2] = prefix[2];
    if (!read_exact(result.frame.data() + 3, body_length, timeout_ms,
                    &result.system_error, &result.error)) {
        result.frame.clear();
        return result;
    }

    // QMUX response flag is bit 7. Validate service/client before parsing QMI.
    if ((result.frame[3] & 0x80U) == 0 || result.frame[4] != expected_service ||
        result.frame[5] != expected_client) {
        result.error = TransportError::UnexpectedFrame;
        result.frame.clear();
        return result;
    }

    const MessageResult message = parse_qmi_message(
        expected_service, result.frame.data() + 6, result.frame.size() - 6);
    if (!message || message.message.transaction_id != expected_transaction_id) {
        result.error = TransportError::UnexpectedFrame;
        result.frame.clear();
    }
    return result;
}

const char* QmiTransport::error_string(TransportError error) {
    switch (error) {
        case TransportError::None: return "none";
        case TransportError::InvalidArgument: return "invalid argument or request frame";
        case TransportError::IoError: return "I/O error";
        case TransportError::Timeout: return "timeout";
        case TransportError::PeerClosed: return "peer closed";
        case TransportError::BadMarker: return "invalid QMUX marker";
        case TransportError::FrameTooLarge: return "frame length out of bounds";
        case TransportError::InvalidFrame: return "invalid frame";
        case TransportError::UnexpectedFrame: return "unexpected service, client, flags, or transaction";
    }
    return "unknown";
}

}  // namespace j5qmi
