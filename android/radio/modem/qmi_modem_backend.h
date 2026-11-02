#pragma once

#include "j5qmi/qmi_client_manager.h"
#include "j5qmi/qmi_transport.h"

#include <mutex>
#include <string>

namespace j5qmi {

class QmiModemBackend {
public:
    QmiModemBackend() = default;
    ~QmiModemBackend();

    QmiModemBackend(const QmiModemBackend&) = delete;
    QmiModemBackend& operator=(const QmiModemBackend&) = delete;

    bool connect();
    void disconnect();

    bool isConnected() const;

    bool getBasebandVersion(std::string* version);
    bool getImei(std::string* imei);
    bool setRadioPower(bool enabled);

private:
    bool openQmi();
    bool openAt();
    void closeAt();
    void closeQmi();

    bool sendAtCommand(
        const std::string& command,
        std::string* response,
        int timeout_ms = 3000);

    static bool extractAtValue(
        const std::string& response,
        const std::string& command,
        std::string* value);

    mutable std::mutex mutex_;

    int qmi_fd_ = -1;
    int at_fd_ = -1;

    QmiTransport* transport_ = nullptr;
    QmiClientManager* clients_ = nullptr;

    bool connected_ = false;
};

}  // namespace j5qmi
