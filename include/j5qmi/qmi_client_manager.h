#pragma once

#include "j5qmi/qmi_transport.h"

#include <cstdint>
#include <map>
#include <mutex>

namespace j5qmi {

struct QmiClientResult {
    TransportError transport_error{TransportError::None};
    uint16_t qmi_error{0};
    uint8_t service{0};
    uint8_t client_id{0};
    bool success{false};

    explicit operator bool() const { return success; }
};

class QmiClientManager {
public:
    explicit QmiClientManager(QmiTransport& transport,
                              int timeout_ms = 3000);

    // Allocate a client for a QMI service.
    // Returns the existing client if this service is already allocated.
    QmiClientResult acquire(uint8_t service);

    // Release a previously allocated client.
    bool release(uint8_t service);

    // Release every allocated client. Returns false if any release failed.
    bool release_all();

    bool has_client(uint8_t service) const;
    uint8_t client_id(uint8_t service) const;
    size_t client_count() const;

private:
    QmiClientResult allocate_locked(uint8_t service);
    bool release_locked(uint8_t service);

    QmiTransport& transport_;
    int timeout_ms_;
    uint8_t next_transaction_id_{1};

    mutable std::mutex mutex_;
    std::map<uint8_t, uint8_t> clients_;
};

}  // namespace j5qmi
