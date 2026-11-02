#include "radio_modem_service.h"

#include <android-base/logging.h>

namespace aidl::android::hardware::radio::modem {

using ::aidl::android::hardware::radio::RadioError;
using ::aidl::android::hardware::radio::RadioResponseType;

RadioModemService::RadioModemService() {
    LOG(INFO) << "j5xnlte RadioModemService starting";

    if (backend_.connect()) {
        LOG(INFO) << "QMI modem connected";
    } else {
        LOG(ERROR) << "QMI modem connection failed";
    }
}

RadioResponseInfo RadioModemService::makeResponseInfo(
    int serial, RadioError error) const {
    RadioResponseInfo info{};
    info.type = RadioResponseType::SOLICITED;
    info.serial = serial;
    info.error = error;
    return info;
}

std::shared_ptr<IRadioModemResponse> RadioModemService::getResponse() {
    std::lock_guard<std::mutex> lock(mutex_);
    return response_;
}

void RadioModemService::sendSimpleError(
    int serial, RadioError error, const char* operation) {
    LOG(WARNING) << operation << ": request not supported";

    auto response = getResponse();
    if (!response) {
        LOG(ERROR) << "Response interface is not registered";
        return;
    }

    const auto info = makeResponseInfo(serial, error);

    if (std::string(operation) == "enableModem") {
        response->enableModemResponse(info);
    } else if (std::string(operation) == "getHardwareConfig") {
        response->getHardwareConfigResponse(info, {});
    } else if (std::string(operation) == "getModemActivityInfo") {
        response->getModemActivityInfoResponse(info, {});
    } else if (std::string(operation) == "getRadioCapability") {
        response->getRadioCapabilityResponse(info, {});
    } else if (std::string(operation) == "nvReadItem") {
        response->nvReadItemResponse(info, "");
    } else if (std::string(operation) == "nvResetConfig") {
        response->nvResetConfigResponse(info);
    } else if (std::string(operation) == "nvWriteCdmaPrl") {
        response->nvWriteCdmaPrlResponse(info);
    } else if (std::string(operation) == "nvWriteItem") {
        response->nvWriteItemResponse(info);
    } else if (std::string(operation) == "requestShutdown") {
        response->requestShutdownResponse(info);
    } else if (std::string(operation) == "sendDeviceState") {
        response->sendDeviceStateResponse(info);
    } else if (std::string(operation) == "setRadioCapability") {
        response->setRadioCapabilityResponse(info, {});
    } else if (std::string(operation) == "setRadioPower") {
        response->setRadioPowerResponse(info);
    }
}

ndk::ScopedAStatus RadioModemService::setResponseFunctions(
    const std::shared_ptr<IRadioModemResponse>& radioModemResponse,
    const std::shared_ptr<IRadioModemIndication>& radioModemIndication) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        response_ = radioModemResponse;
        indication_ = radioModemIndication;
    }

    LOG(INFO) << "Radio modem callbacks registered";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getBasebandVersion(int serial) {
    auto response = getResponse();
    if (!response) return ndk::ScopedAStatus::ok();

    std::string version;
    const bool success = backend_.getBasebandVersion(&version);

    auto info = makeResponseInfo(
        serial, success ? RadioError::NONE : RadioError::RADIO_NOT_AVAILABLE);

    response->getBasebandVersionResponse(info, success ? version : "");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::setRadioPower(
    int serial, bool powerOn, bool /*forEmergencyCall*/,
    bool /*preferredForEmergencyCall*/) {
    auto response = getResponse();
    if (!response) return ndk::ScopedAStatus::ok();

    const bool success = backend_.setRadioPower(powerOn);

    if (success) {
        std::lock_guard<std::mutex> lock(mutex_);
        modemEnabled_ = powerOn;
    }

    auto info = makeResponseInfo(
        serial, success ? RadioError::NONE : RadioError::RADIO_NOT_AVAILABLE);

    response->setRadioPowerResponse(info);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::enableModem(int serial, bool on) {
    auto response = getResponse();
    if (!response) return ndk::ScopedAStatus::ok();

    const bool success = backend_.setRadioPower(on);

    if (success) {
        std::lock_guard<std::mutex> lock(mutex_);
        modemEnabled_ = on;
    }

    response->enableModemResponse(makeResponseInfo(
        serial, success ? RadioError::NONE : RadioError::RADIO_NOT_AVAILABLE));

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getModemStackStatus(int serial) {
    auto response = getResponse();
    if (!response) return ndk::ScopedAStatus::ok();

    bool enabled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled = modemEnabled_;
    }

    response->getModemStackStatusResponse(
        makeResponseInfo(serial, RadioError::NONE), enabled);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getImei(int serial) {
    auto response = getResponse();
    if (!response) return ndk::ScopedAStatus::ok();

    response->getImeiResponse(
        makeResponseInfo(serial, RadioError::REQUEST_NOT_SUPPORTED), nullptr);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getDeviceIdentity(int serial) {
    auto response = getResponse();
    if (!response) return ndk::ScopedAStatus::ok();

    response->getDeviceIdentityResponse(
        makeResponseInfo(serial, RadioError::REQUEST_NOT_SUPPORTED),
        "", "", "", "");

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getHardwareConfig(int serial) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "getHardwareConfig");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getModemActivityInfo(int serial) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "getModemActivityInfo");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::getRadioCapability(int serial) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "getRadioCapability");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::nvReadItem(int serial, NvItem /*itemId*/) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "nvReadItem");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::nvResetConfig(
    int serial, ResetNvType /*resetType*/) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "nvResetConfig");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::nvWriteCdmaPrl(
    int serial, const std::vector<uint8_t>& /*prl*/) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "nvWriteCdmaPrl");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::nvWriteItem(
    int serial, const NvWriteItem& /*item*/) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "nvWriteItem");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::requestShutdown(int serial) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "requestShutdown");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::sendDeviceState(
    int serial, DeviceStateType /*deviceStateType*/, bool /*state*/) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "sendDeviceState");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::setRadioCapability(
    int serial, const RadioCapability& /*rc*/) {
    sendSimpleError(serial, RadioError::REQUEST_NOT_SUPPORTED, "setRadioCapability");
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus RadioModemService::responseAcknowledgement() {
    return ndk::ScopedAStatus::ok();
}

} // namespace aidl::android::hardware::radio::modem
