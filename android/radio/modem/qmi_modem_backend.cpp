#include "qmi_modem_backend.h"

#include <android-base/logging.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace j5qmi {
namespace {

constexpr char kQmiDevice[] = "/dev/wwan0qmi0";
constexpr char kAtDevice[] = "/dev/wwan0at0";

constexpr int kDefaultTimeoutMs = 3000;

// QMI service identifiers.
constexpr uint8_t kQmiServiceDms = 0x02;
constexpr uint8_t kQmiServiceNas = 0x03;
constexpr uint8_t kQmiServiceWms = 0x05;
constexpr uint8_t kQmiServiceWds = 0x01;

bool containsTerminalResponse(const std::string& response) {
    return response.find("\r\nOK\r\n") != std::string::npos ||
           response.find("\nOK\n") != std::string::npos ||
           response.find("\r\nERROR\r\n") != std::string::npos ||
           response.find("+CME ERROR") != std::string::npos ||
           response.find("+CMS ERROR") != std::string::npos;
}

bool isAtSuccess(const std::string& response) {
    return response.find("\r\nOK\r\n") != std::string::npos ||
           response.find("\nOK\n") != std::string::npos;
}

}  // namespace

QmiModemBackend::~QmiModemBackend() {
    disconnect();
}

bool QmiModemBackend::openQmi() {
    qmi_fd_ = open(kQmiDevice, O_RDWR | O_CLOEXEC);

    if (qmi_fd_ < 0) {
        PLOG(ERROR) << "Cannot open " << kQmiDevice;
        return false;
    }

    transport_ = new QmiTransport(qmi_fd_);
    clients_ = new QmiClientManager(*transport_, kDefaultTimeoutMs);

    // DMS and NAS are required for basic modem information and network state.
    const QmiClientResult dms = clients_->acquire(kQmiServiceDms);
    if (!dms) {
        LOG(ERROR) << "Failed to allocate QMI DMS client"
                   << " transport_error="
                   << QmiTransport::error_string(dms.transport_error)
                   << " qmi_error=" << dms.qmi_error;
        return false;
    }

    const QmiClientResult nas = clients_->acquire(kQmiServiceNas);
    if (!nas) {
        LOG(ERROR) << "Failed to allocate QMI NAS client"
                   << " transport_error="
                   << QmiTransport::error_string(nas.transport_error)
                   << " qmi_error=" << nas.qmi_error;
        return false;
    }

    // These clients are allocated for the later WDS/WMS implementation.
    // Their allocation does not mean data or SMS is implemented yet.
    const QmiClientResult wds = clients_->acquire(kQmiServiceWds);
    if (!wds) {
        LOG(WARNING) << "QMI WDS client allocation failed"
                     << " qmi_error=" << wds.qmi_error;
    }

    const QmiClientResult wms = clients_->acquire(kQmiServiceWms);
    if (!wms) {
        LOG(WARNING) << "QMI WMS client allocation failed"
                     << " qmi_error=" << wms.qmi_error;
    }

    LOG(INFO) << "QMI clients allocated: DMS=" << dms.client_id
              << " NAS=" << nas.client_id;

    return true;
}

bool QmiModemBackend::openAt() {
    at_fd_ = open(kAtDevice, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

    if (at_fd_ < 0) {
        PLOG(ERROR) << "Cannot open " << kAtDevice;
        return false;
    }

    termios tty{};
    if (tcgetattr(at_fd_, &tty) != 0) {
        PLOG(ERROR) << "tcgetattr failed for " << kAtDevice;
        closeAt();
        return false;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(at_fd_, TCSANOW, &tty) != 0) {
        PLOG(ERROR) << "tcsetattr failed for " << kAtDevice;
        closeAt();
        return false;
    }

    tcflush(at_fd_, TCIOFLUSH);
    return true;
}

void QmiModemBackend::closeAt() {
    if (at_fd_ >= 0) {
        close(at_fd_);
        at_fd_ = -1;
    }
}

void QmiModemBackend::closeQmi() {
    if (clients_ != nullptr) {
        clients_->release_all();
        delete clients_;
        clients_ = nullptr;
    }

    delete transport_;
    transport_ = nullptr;

    if (qmi_fd_ >= 0) {
        close(qmi_fd_);
        qmi_fd_ = -1;
    }
}

bool QmiModemBackend::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_) {
        return true;
    }

    LOG(INFO) << "Connecting to j5xnlte modem";

    if (!openQmi()) {
        closeQmi();
        return false;
    }

    if (!openAt()) {
        closeAt();
        closeQmi();
        return false;
    }

    std::string response;
    if (!sendAtCommand("AT", &response)) {
        LOG(ERROR) << "AT interface did not respond";
        closeAt();
        closeQmi();
        return false;
    }

    connected_ = true;
    LOG(INFO) << "QMI and AT interfaces are ready";

    return true;
}

void QmiModemBackend::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    connected_ = false;
    closeAt();
    closeQmi();
}

bool QmiModemBackend::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && qmi_fd_ >= 0 && at_fd_ >= 0;
}

bool QmiModemBackend::sendAtCommand(
    const std::string& command,
    std::string* response,
    int timeout_ms) {
    if (at_fd_ < 0 || response == nullptr || timeout_ms < 0) {
        return false;
    }

    response->clear();

    const std::string request = command + "\r";

    size_t written = 0;
    while (written < request.size()) {
        pollfd pfd{};
        pfd.fd = at_fd_;
        pfd.events = POLLOUT;

        const int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result <= 0) {
            if (poll_result < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }

        const ssize_t count = write(
            at_fd_, request.data() + written, request.size() - written);

        if (count > 0) {
            written += static_cast<size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }

        PLOG(ERROR) << "AT command write failed";
        return false;
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    char buffer[256];

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now).count();

        pollfd pfd{};
        pfd.fd = at_fd_;
        pfd.events = POLLIN;

        const int poll_result = poll(
            &pfd, 1, static_cast<int>(remaining));

        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }

            PLOG(ERROR) << "AT response poll failed";
            return false;
        }

        if (poll_result == 0) {
            break;
        }

        const ssize_t count = read(at_fd_, buffer, sizeof(buffer));

        if (count > 0) {
            response->append(buffer, static_cast<size_t>(count));

            if (containsTerminalResponse(*response)) {
                return isAtSuccess(*response);
            }

            continue;
        }

        if (count < 0 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }

        if (count < 0) {
            PLOG(ERROR) << "AT response read failed";
            return false;
        }
    }

    LOG(WARNING) << "AT command timed out: " << command;
    return false;
}

bool QmiModemBackend::extractAtValue(
    const std::string& response,
    const std::string& command,
    std::string* value) {
    if (value == nullptr) {
        return false;
    }

    size_t start = 0;

    while (start < response.size()) {
        const size_t end = response.find('\n', start);
        std::string line = response.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start);

        while (!line.empty() &&
               (line.back() == '\r' || line.back() == '\n' ||
                line.back() == ' ')) {
            line.pop_back();
        }

        const size_t first = line.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            line.erase(0, first);
        }

        if (!line.empty() && line != command && line != "OK" &&
            line != "ERROR") {
            *value = line;
            return true;
        }

        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    return false;
}

bool QmiModemBackend::getBasebandVersion(std::string* version) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || version == nullptr) {
        return false;
    }

    std::string response;
    if (!sendAtCommand("AT+CGMR", &response)) {
        return false;
    }

    if (!extractAtValue(response, "AT+CGMR", version)) {
        LOG(ERROR) << "AT+CGMR returned no version";
        return false;
    }

    return true;
}

bool QmiModemBackend::getImei(std::string* imei) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_ || imei == nullptr) {
        return false;
    }

    std::string response;
    if (!sendAtCommand("AT+CGSN", &response)) {
        return false;
    }

    if (!extractAtValue(response, "AT+CGSN", imei)) {
        LOG(ERROR) << "AT+CGSN returned no device identity";
        return false;
    }

    return true;
}

bool QmiModemBackend::setRadioPower(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!connected_) {
        return false;
    }

    std::string response;
    const std::string command = enabled ? "AT+CFUN=1" : "AT+CFUN=0";

    if (!sendAtCommand(command, &response, 10000)) {
        LOG(ERROR) << "Failed to change modem radio state";
        return false;
    }

    LOG(INFO) << "Modem radio state changed: "
              << (enabled ? "enabled" : "disabled");

    return true;
}

}  // namespace j5qmi
