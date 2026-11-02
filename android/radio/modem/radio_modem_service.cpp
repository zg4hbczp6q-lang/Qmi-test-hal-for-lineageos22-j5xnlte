#include "radio_modem_service.h"

#include <android/binder_status.h>
#include <android-base/logging.h>

namespace aidl::android::hardware::radio::modem {

RadioModemService::RadioModemService() {
    LOG(INFO) << "j5xnlte RadioModemService created";

    if (!backend_.connect()) {
        LOG(ERROR) << "Failed to connect to QMI modem";
    } else {
        LOG(INFO) << "QMI modem connected";
    }
}

ndk::ScopedAStatus RadioModemService::setResponseFunctions(
    const std::shared_ptr<IRadioModemResponse>& radioModemResponse,
    const std::shared_ptr<IRadioModemIndication>& radioModemIndication
) {
    std::lock_guard<std::mutex> lock(mutex_);

    response_ = radioModemResponse;
    indication_ = radioModemIndication;

    LOG(INFO) << "Radio modem response callbacks registered";

    return ndk::ScopedAStatus::ok();
}

} // namespace aidl::android::hardware::radio::modem
