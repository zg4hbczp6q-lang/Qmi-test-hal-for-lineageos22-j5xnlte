#include "j5qmi/qmi_frame.h"
#include "j5qmi/qmi_message.h"
#include "j5qmi/qmi_transport.h"

#include <cassert>
#include <cstdint>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

int main() {
    int sv[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    const auto request_qmi = j5qmi::encode_qmi_message(0x03, 0x00, 0x1234,
                                                       0x0024, {});
    const auto request = j5qmi::encode_qmux(0x00, 0x03, 0x01, request_qmi);
    const auto response_qmi = j5qmi::encode_qmi_message(0x03, 0x02, 0x1234,
                                                        0x0024, {});
    const auto response = j5qmi::encode_qmux(0x80, 0x03, 0x01, response_qmi);

    std::thread modem([&]() {
        std::vector<uint8_t> received(request.size());
        size_t off = 0;
        while (off < received.size()) {
            const ssize_t n = read(sv[1], received.data() + off, received.size() - off);
            assert(n > 0);
            off += static_cast<size_t>(n);
        }
        // Deliberately fragment the response to exercise stream reassembly.
        assert(write(sv[1], response.data(), 2) == 2);
        usleep(1000);
        assert(write(sv[1], response.data() + 2, response.size() - 2) ==
               static_cast<ssize_t>(response.size() - 2));
    });

    j5qmi::QmiTransport transport(sv[0]);
    const auto result = transport.transact(request, 0x03, 0x01, 0x1234, 1000);
    assert(result);
    assert(result.frame == response);
    modem.join();
    close(sv[0]);
    close(sv[1]);

    int timeout_pair[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, timeout_pair) == 0);
    j5qmi::QmiTransport timeout_transport(timeout_pair[0]);
    const auto timeout = timeout_transport.transact(request, 0x03, 0x01, 0x1234, 10);
    assert(!timeout);
    assert(timeout.error == j5qmi::TransportError::Timeout);
    close(timeout_pair[0]);
    close(timeout_pair[1]);

    auto malformed = request;
    malformed[1] = 0;
    j5qmi::QmiTransport invalid_transport(-1);
    const auto invalid = invalid_transport.transact(malformed, 0x03, 0x01, 0x1234, 10);
    assert(!invalid);
    assert(invalid.error == j5qmi::TransportError::InvalidArgument);
    return 0;
}
