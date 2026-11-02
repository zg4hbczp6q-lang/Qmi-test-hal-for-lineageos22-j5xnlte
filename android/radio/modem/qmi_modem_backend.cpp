#include "qmi_modem_backend.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <vector>

#include "j5qmi/qmi_message.h"

namespace j5qmi {

namespace {

constexpr uint16_t QMI_DMS_GET_REVISION_ID = 0x0021;
constexpr uint16_t QMI_DMS_SET_OPERATING_MODE = 0x002E;

constexpr uint8_t QMI_RESULT_TLV = 0x02;
constexpr uint8_t QMI_REVISION_TLV = 0x01;
constexpr uint8_t QMI_OPERATING_MODE_TLV = 0x01;

constexpr uint8_t QMI_MODE_ONLINE = 0x00;
constexpr uint8_t QMI_MODE_OFFLINE = 0x03;

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

    const QmiClientResult result = clients_->acquire(kDmsService);

    if (!result.success) {
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

    const uint8_t client = clients_->client_id(kDmsService);

    const auto request = encode_qmi_message(
        kDmsService,
        0x00,
        1,
        QMI_DMS_GET_REVISION_ID,
        {}
    );

    const auto frame = encode_qmi_frame(
        kDmsService,
        client,
        1,
        request
    );

    const TransportResult result = transport_->transact(
        frame,
        kDmsService,
        client,
        1,
        3000
    );

    if (!result.success) {
        return false;
    }

    const QmiMessage response = parse_qmi_message(
        kDmsService,
        result.payload.data(),
        result.payload.size()
    );

    uint16_t qmi_result = 0;
    uint16_t qmi_error = 0;

    if (!decode_result_tlv(response, &qmi_result, &qmi_error)) {
        return false;
    }

    if (qmi_result != 0) {
        return false;
    }

    const Tlv* revision = find_tlv(response, QMI_REVISION_TLV);

    if (revision == nullptr || revision->value.empty()) {
        return false;
    }

    version->assign(
        revision->value.begin(),
        revision->value.end()
    );

    return true;
}

bool QmiModemBackend::setRadioPower(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!openDeviceLocked()) {
        return false;
    }

    const uint8_t client = clients_->client_id(kDmsService);

    Tlv operating_mode;
    operating_mode.type = QMI_OPERATING_MODE_TLV;
    operating_mode.value = {
        enabled ? QMI_MODE_ONLINE : QMI_MODE_OFFLINE
    };

    const auto request = encode_qmi_message(
        kDmsService,
        0x00,
        2,
        QMI_DMS_SET_OPERATING_MODE,
        {operating_mode}
    );

    const auto frame = encode_qmi_frame(
        kDmsService,
        client,
        2,
        request
    );

    const TransportResult result = transport_->transact(
        frame,
        kDmsService,
        client,
        2,
        5000
    );

    if (!result.success) {
        return false;
    }

    const QmiMessage response = parse_qmi_message(
        kDmsService,
        result.payload.data(),
        result.payload.size()
    );

    uint16_t qmi_result = 0;
    uint16_t qmi_error = 0;

    if (!decode_result_tlv(response, &qmi_result, &qmi_error)) {
        return false;
    }

    return qmi_result == 0;
}

} // namespace j5qmi
