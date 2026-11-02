#include "qmi_modem_backend.h"

#include <cerrno>
#include <chrono>
#include <cctype>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "j5qmi/qmi_frame.h"
#include "j5qmi/qmi_message.h"

namespace j5qmi {
namespace {

constexpr uint16_t kDmsGetRevisionId = 0x0021;
constexpr uint16_t kDmsSetOperatingMode = 0x002E;

constexpr uint8_t kDmsRevisionTlv = 0x01;
constexpr uint8_t kDmsOperatingModeTlv = 0x01;

constexpr uint8_t kDmsModeOnline = 0x00;
constexpr uint8_t kDmsModeOffline = 0x03;

constexpr int kQmiTimeoutMs = 3000;
constexpr int kAtTimeoutMs = 3000;

bool parse_dms_response(const TransportResult& transport_result,
                        uint8_t client_id,
                        uint16_t expected_message_id,
                        QmiMessage* message) {
    if (!transport_result || message == nullptr) {
        return false;
    }

    const ParseResult frame = parse_qmux(
        transport_result.frame.data(),
        transport_result.frame.size());

    if (!frame ||
        frame.frame.service != 0x02 ||
        frame.frame.client_id != client_id) {
        return false;
    }

    const MessageResult parsed = parse_qmi_message(
        0x02,
        frame.frame.qmi_message.data(),
        frame.frame.qmi_message.size());

    if (!parsed || parsed.message.message_id != expected_message_id) {
        return false;
    }

    *message = parsed.message;
    return true;
}

bool qmi_result_success(const QmiMessage& message) {
    uint16_t result = 0;
    uint16_t error = 0;

    return decode_result_tlv(message, &result, &error) && result == 0;
}

bool is_imei_line(const std::string& line) {
    if (line.size() != 15) {
        return false;
    }

    for (const unsigned char ch : line) {
        if (!std::isdigit(ch)) {
            return false;
        }
    }

    return true;
}

std::string trim_line(const std::string& line) {
    const size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const size_t last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

} // namespace

QmiModemBackend::QmiModemBackend() = default;

QmiModemBackend::~QmiModemBackend() {
    disconnect();
}

bool QmiModemBackend::openDeviceLocked() {
    if (connected_) {
        return true;
    }

    fd_ = open(kQmiDevice, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }

    transport_ = std::make_unique<QmiTransport>(fd_);
    clients_ = std::make_unique<QmiClientManager>(*transport_);

    const QmiClientResult client = clients_->acquire(kDmsService);

    if (!client.success || client.client_id == 0) {
        clients_.reset();
        transport_.reset();
        close(fd_);
        fd_ = -1;
        return false;
    }

    connected_ = true;
    return true;
}

void QmiModemBackend::closeDeviceLocked() {
    if (clients_) {
        clients_->release_all();
        clients_.reset();
    }

    transport_.reset();

    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    connected_ = false;
}

bool QmiModemBackend::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    return openDeviceLocked();
}

void QmiModemBackend::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    closeDeviceLocked();
}

bool QmiModemBackend::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

bool QmiModemBackend::getBasebandVersion(std::string* version) {
    if (version == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (!openDeviceLocked()) {
        return false;
    }

    const uint8_t client_id = clients_->client_id(kDmsService);
    if (client_id == 0) {
        return false;
    }

    constexpr uint16_t transaction_id = 1;

    const std::vector<uint8_t> qmi_message = encode_qmi_message(
        kDmsService,
        0x00,
        transaction_id,
        kDmsGetRevisionId,
        {});

    const std::vector<uint8_t> request = encode_qmux(
        0x00,
        kDmsService,
        client_id,
        qmi_message);

    if (request.empty()) {
        return false;
    }

    const TransportResult response = transport_->transact(
        request,
        kDmsService,
        client_id,
        transaction_id,
        kQmiTimeoutMs);

    QmiMessage message{};

    if (!parse_dms_response(
            response,
            client_id,
            kDmsGetRevisionId,
            &message) ||
        !qmi_result_success(message)) {
        return false;
    }

    const Tlv* revision = find_tlv(message, kDmsRevisionTlv);

    if (revision == nullptr || revision->value.empty()) {
        return false;
    }

    version->assign(
        revision->value.begin(),
        revision->value.end());

    if (!version->empty() && version->back() == '\0') {
        version->pop_back();
    }

    return !version->empty();
}

bool QmiModemBackend::getImeiFromAtLocked(std::string* imei) {
    if (imei == nullptr) {
        return false;
    }

    const int at_fd = open(
        kAtDevice,
        O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);

    if (at_fd < 0) {
        return false;
    }

    bool success = false;
    std::string received;

    const char command[] = "AT+CGSN\r";
    size_t written = 0;

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kAtTimeoutMs);

    while (written < sizeof(command) - 1) {
        const ssize_t count = write(
            at_fd,
            command + written,
            sizeof(command) - 1 - written);

        if (count > 0) {
            written += static_cast<size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{at_fd, POLLOUT, 0};
            if (poll(&pfd, 1, 250) > 0 && (pfd.revents & POLLOUT)) {
                continue;
            }
        }

        close(at_fd);
        return false;
    }

    char buffer[128];

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();

        pollfd pfd{at_fd, POLLIN, 0};
        const int poll_result = poll(
            &pfd,
            1,
            static_cast<int>(remaining));

        if (poll_result < 0 && errno == EINTR) {
            continue;
        }

        if (poll_result <= 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }

        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        const ssize_t count = read(at_fd, buffer, sizeof(buffer));

        if (count < 0 && (errno == EINTR ||
                          errno == EAGAIN ||
                          errno == EWOULDBLOCK)) {
            continue;
        }

        if (count <= 0) {
            break;
        }

        received.append(buffer, static_cast<size_t>(count));

        if (received.size() > 1024) {
            break;
        }

        size_t line_start = 0;

        while (line_start < received.size()) {
            const size_t line_end = received.find('\n', line_start);
            if (line_end == std::string::npos) {
                break;
            }

            const std::string line = trim_line(
                received.substr(line_start, line_end - line_start));

            if (is_imei_line(line)) {
                *imei = line;
            }

            if (line == "OK") {
                success = is_imei_line(*imei);
                close(at_fd);
                return success;
            }

            if (line == "ERROR" || line == "+CME ERROR" ||
                line.compare(0, 10, "+CME ERROR") == 0) {
                close(at_fd);
                return false;
            }

            line_start = line_end + 1;
        }
    }

    close(at_fd);

    if (!success) {
        imei->clear();
    }

    return success;
}

bool QmiModemBackend::getImei(std::string* imei) {
    if (imei == nullptr) {
        return false;
    }

    imei->clear();

    std::lock_guard<std::mutex> lock(mutex_);
    return getImeiFromAtLocked(imei);
}

bool QmiModemBackend::setRadioPower(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!openDeviceLocked()) {
        return false;
    }

    const uint8_t client_id = clients_->client_id(kDmsService);
    if (client_id == 0) {
        return false;
    }

    constexpr uint16_t transaction_id = 2;

    const Tlv operating_mode{
        kDmsOperatingModeTlv,
        {enabled ? kDmsModeOnline : kDmsModeOffline}
    };

    const std::vector<uint8_t> qmi_message = encode_qmi_message(
        kDmsService,
        0x00,
        transaction_id,
        kDmsSetOperatingMode,
        {operating_mode});

    const std::vector<uint8_t> request = encode_qmux(
        0x00,
        kDmsService,
        client_id,
        qmi_message);

    if (request.empty()) {
        return false;
    }

    const TransportResult response = transport_->transact(
        request,
        kDmsService,
        client_id,
        transaction_id,
        5000);

    QmiMessage message{};

    return parse_dms_response(
               response,
               client_id,
               kDmsSetOperatingMode,
               &message) &&
           qmi_result_success(message);
}

} // namespace j5qmi
