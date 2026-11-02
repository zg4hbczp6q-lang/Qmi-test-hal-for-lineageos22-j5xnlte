
#include "qmi_modem_backend.h"

#include <fcntl.h>
#include <unistd.h>

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
