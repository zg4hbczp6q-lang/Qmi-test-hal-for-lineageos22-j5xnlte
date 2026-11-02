#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "j5qmi/qmi_client_manager.h"
#include "j5qmi/qmi_transport.h"

namespace j5qmi {

class QmiModemBackend {
public:
    static constexpr const char* kQmiDevice = "/dev/wwan0qmi0";

    QmiModemBackend();
    ~QmiModemBackend();

    QmiModemBackend(const QmiModemBackend&) = delete;
    QmiModemBackend& operator=(const QmiModemBackend&) = delete;

    bool connect();
    void disconnect();

    bool isConnected() const;

    bool getBasebandVersion(std::string* version);

    bool setRadioPower(bool enabled);

private:
    bool openDeviceLocked();
    void closeDeviceLocked();

    mutable std::mutex mutex_;

    int fd_ = -1;

    std::unique_ptr<QmiTransport> transport_;
    std::unique_ptr<QmiClientManager> clients_;

    bool connected_ = false;

    static constexpr uint8_t kDmsService = 0x02;
};

} // namespace j5qmi
