#include "j5qmi/qmi_client_manager.h"
#include "j5qmi/qmi_frame.h"
#include "j5qmi/qmi_message.h"

#include <cassert>
#include <cstdint>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace j5qmi;

namespace {

std::vector<uint8_t> make_response(
    uint16_t message_id,
    uint8_t transaction_id,
    const std::vector<Tlv>& tlvs) {

    const auto qmi = encode_qmi_message(
        0x00, 0x02, transaction_id, message_id, tlvs);

    return encode_qmux(0x80, 0x00, 0x00, qmi);
}

void write_all(int fd, const std::vector<uint8_t>& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = write(
            fd, data.data() + offset, data.size() - offset);
        assert(n > 0);
        offset += static_cast<size_t>(n);
    }
}

std::vector<uint8_t> read_frame(int fd) {
    uint8_t header[3]{};
    assert(read(fd, header, sizeof(header)) ==
           static_cast<ssize_t>(sizeof(header)));

    const size_t body_size =
        static_cast<size_t>(header[1]) |
        (static_cast<size_t>(header[2]) << 8);

    std::vector<uint8_t> frame(3 + body_size);
    frame[0] = header[0];
    frame[1] = header[1];
    frame[2] = header[2];

    size_t offset = 0;
    while (offset < body_size) {
        const ssize_t n = read(
            fd, frame.data() + 3 + offset, body_size - offset);
        assert(n > 0);
        offset += static_cast<size_t>(n);
    }

    return frame;
}

void test_acquire_and_release() {
    int sockets[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    QmiTransport transport(sockets[0]);
    QmiClientManager manager(transport);

    std::thread modem([&]() {
        // GET_CLIENT_ID request.
        const auto get_request = read_frame(sockets[1]);
        const auto get_frame = parse_qmux(
            get_request.data(), get_request.size());
        assert(get_frame);

        const auto get_message = parse_qmi_message(
            0x00,
            get_frame.frame.qmi_message.data(),
            get_frame.frame.qmi_message.size());

        assert(get_message);
        assert(get_message.message.message_id == 0x0022);

        const uint8_t get_txn =
            static_cast<uint8_t>(get_message.message.transaction_id);

        write_all(sockets[1], make_response(
            0x0022, get_txn,
            {
                Tlv{0x01, {0x03, 0x01}},
                Tlv{0x02, {0x00, 0x00, 0x00, 0x00}}
            }));

        // RELEASE_CLIENT_ID request.
        const auto release_request = read_frame(sockets[1]);
        const auto release_frame = parse_qmux(
            release_request.data(), release_request.size());
        assert(release_frame);

        const auto release_message = parse_qmi_message(
            0x00,
            release_frame.frame.qmi_message.data(),
            release_frame.frame.qmi_message.size());

        assert(release_message);
        assert(release_message.message.message_id == 0x0023);

        const uint8_t release_txn =
            static_cast<uint8_t>(release_message.message.transaction_id);

        write_all(sockets[1], make_response(
            0x0023, release_txn,
            {
                Tlv{0x02, {0x00, 0x00, 0x00, 0x00}}
            }));
    });

    const QmiClientResult client = manager.acquire(0x03);
    assert(client.success);
    assert(client.service == 0x03);
    assert(client.client_id == 0x01);
    assert(manager.has_client(0x03));
    assert(manager.client_count() == 1);

    assert(manager.release(0x03));
    assert(!manager.has_client(0x03));
    assert(manager.client_count() == 0);

    modem.join();
    close(sockets[0]);
    close(sockets[1]);
}

void test_reject_ctl_service() {
    int sockets[2]{};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    QmiTransport transport(sockets[0]);
    QmiClientManager manager(transport);

    const QmiClientResult result = manager.acquire(0x00);
    assert(!result.success);
    assert(result.transport_error == TransportError::InvalidArgument);

    close(sockets[0]);
    close(sockets[1]);
}

}  // namespace

int main() {
    test_acquire_and_release();
    test_reject_ctl_service();
    return 0;
}
