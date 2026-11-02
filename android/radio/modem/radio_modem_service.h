#pragma once

#include <memory>
#include <mutex>

#include <android/binder_auto_utils.h>
#include <android/binder_interface_utils.h>

#include <aidl/android/hardware/radio/modem/BnRadioModem.h>
#include <aidl/android/hardware/radio/modem/IRadioModemResponse.h>
#include <aidl/android/hardware/radio/modem/IRadioModemIndication.h>

#include "qmi_modem_backend.h"

namespace aidl::android::hardware::radio::modem {

class RadioModemService final : public BnRadioModem {
public:
    RadioModemService();

    ndk::ScopedAStatus setResponseFunctions(
        const std::shared_ptr<IRadioModemResponse>& radioModemResponse,
        const std::shared_ptr<IRadioModemIndication>& radioModemIndication
    ) override;

private:
    std::mutex mutex_;

    std::shared_ptr<IRadioModemResponse> response_;
    std::shared_ptr<IRadioModemIndication> indication_;

    j5qmi::QmiModemBackend backend_;
};

} // namespace aidl::android::hardware::radio::modem
