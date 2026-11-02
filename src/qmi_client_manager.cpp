#include "j5qmi/qmi_client_manager.h"

#include "j5qmi/qmi_frame.h"
#include "j5qmi/qmi_message.h"

#include <vector>

namespace j5qmi {
namespace {

constexpr uint8_t kCtlService = 0x00;
constexpr uint8_t kCtlClient = 0x00;

constexpr uint16_t kCtlGetClientId = 0x0022;
constexpr uint16_t kCtlReleaseClientId = 0x0023;

constexpr uint8_t kCtlClientIdTlv = 0x01;
constexpr uint8_t kQmiResultTlv = 0x02;

}  // namespace

QmiClientManager::QmiClientManager(QmiTransport& transport,
                                   int timeout_ms)
    : transport_(transport),
      timeout_ms_(timeout_ms) {}

QmiClientResult QmiClientManager::acquire(uint8_t service) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto existing = clients_.find(service);
    if (existing != clients_.end()) {
        QmiClientResult result{};
        result.service = service;
        result.client_id = existing->second;
        result.success = true;
        return result;
    }

    return allocate_locked(service);
}

QmiClientResult QmiClientManager::allocate_locked(uint8_t service) {
    QmiClientResult result{};
    result.service = service;

    if (service == kCtlService || timeout_ms_ < 0) {
        result.transport_error = TransportError::InvalidArgument;
        return result;
    }

    const uint8_t transaction_id = next_transaction_id_++;

    // CTL GET_CLIENT_ID request:
    // TLV 0x01, length 1, requested service type.
    const std::vector<Tlv> tlvs{
        Tlv{kCtlClientIdTlv, {service}}
    };

    const auto qmi_message = encode_qmi_message(
        kCtlService, 0x00, transaction_id, kCtlGetClientId, tlvs);

    const auto request = encode_qmux(
        0x00, kCtlService, kCtlClient, qmi_message);

    if (request.empty()) {
        result.transport_error = TransportError::InvalidArgument;
        return result;
    }

    const TransportResult response = transport_.transact(
        request, kCtlService, kCtlClient, transaction_id, timeout_ms_);

    if (!response) {
        result.transport_error = response.error;
        return result;
    }

    const ParseResult frame = parse_qmux(
        response.frame.data(), response.frame.size());

    if (!frame) {
        result.transport_error = TransportError::InvalidFrame;
        return result;
    }

    const MessageResult message = parse_qmi_message(
        kCtlService,
        frame.frame.qmi_message.data(),
        frame.frame.qmi_message.size());

    if (!message || message.message.message_id != kCtlGetClientId) {
        result.transport_error = TransportError::InvalidFrame;
        return result;
    }

    uint16_t qmi_result = 0;
    uint16_t qmi_error = 0;

    if (!decode_result_tlv(message.message, &qmi_result, &qmi_error)) {
        result.transport_error = TransportError::InvalidFrame;
        return result;
    }

    if (qmi_result != 0) {
        result.qmi_error = qmi_error;
        return result;
    }

    const Tlv* client_tlv = find_tlv(message.message, kCtlClientIdTlv);

    // GET_CLIENT_ID response TLV:
    // byte 0 = service type, byte 1 = allocated client ID.
    if (client_tlv == nullptr || client_tlv->value.size() != 2 ||
        client_tlv->value[0] != service ||
        client_tlv->value[1] == 0) {
        result.transport_error = TransportError::InvalidFrame;
        return result;
    }

    result.client_id = client_tlv->value[1];
    result.success = true;

    clients_[service] = result.client_id;
    return result;
}

bool QmiClientManager::release(uint8_t service) {
    std::lock_guard<std::mutex> lock(mutex_);
    return release_locked(service);
}

bool QmiClientManager::release_locked(uint8_t service) {
    const auto it = clients_.find(service);
    if (it == clients_.end()) {
        return true;
    }

    const uint8_t transaction_id = next_transaction_id_++;

    // CTL RELEASE_CLIENT_ID request:
    // TLV 0x01, length 2, service type + client ID.
    const std::vector<Tlv> tlvs{
        Tlv{kCtlClientIdTlv, {service, it->second}}
    };

    const auto qmi_message = encode_qmi_message(
        kCtlService, 0x00, transaction_id, kCtlReleaseClientId, tlvs);

    const auto request = encode_qmux(
        0x00, kCtlService, kCtlClient, qmi_message);

    if (request.empty()) {
        return false;
    }

    const TransportResult response = transport_.transact(
        request, kCtlService, kCtlClient, transaction_id, timeout_ms_);

    if (!response) {
        return false;
    }

    const ParseResult frame = parse_qmux(
        response.frame.data(), response.frame.size());

    if (!frame) {
        return false;
    }

    const MessageResult message = parse_qmi_message(
        kCtlService,
        frame.frame.qmi_message.data(),
        frame.frame.qmi_message.size());

    if (!message ||
        message.message.message_id != kCtlReleaseClientId) {
        return false;
    }

    uint16_t qmi_result = 0;
    uint16_t qmi_error = 0;

    if (!decode_result_tlv(message.message, &qmi_result, &qmi_error) ||
        qmi_result != 0) {
        return false;
    }

    clients_.erase(it);
    return true;
}

bool QmiClientManager::release_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    bool success = true;

    while (!clients_.empty()) {
        const uint8_t service = clients_.begin()->first;

        if (!release_locked(service)) {
            success = false;

            // Avoid an infinite loop if the modem refuses a release.
            clients_.erase(service);
        }
    }

    return success;
}

bool QmiClientManager::has_client(uint8_t service) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.find(service) != clients_.end();
}

uint8_t QmiClientManager::client_id(uint8_t service) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = clients_.find(service);
    return it == clients_.end() ? 0 : it->second;
}

size_t QmiClientManager::client_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_.size();
}

}  // namespace j5qmi
