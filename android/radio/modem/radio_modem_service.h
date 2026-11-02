#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <string>

#include <android/binder_auto_utils.h>
#include <android/binder_interface_utils.h>

#include <aidl/android/hardware/radio/RadioResponseInfo.h>
#include <aidl/android/hardware/radio/RadioError.h>
#include <aidl/android/hardware/radio/RadioResponseType.h>

#include <aidl/android/hardware/radio/modem/BnRadioModem.h>
#include <aidl/android/hardware/radio/modem/IRadioModemResponse.h>
#include <aidl/android/hardware/radio/modem/IRadioModemIndication.h>
#include <aidl/android/hardware/radio/modem/DeviceStateType.h>
#include <aidl/android/hardware/radio/modem/NvItem.h>
#include <aidl/android/hardware/radio/modem/NvWriteItem.h>
#include <aidl/android/hardware/radio/modem/RadioCapability.h>
#include <aidl/android/hardware/radio/modem/ResetNvType.h>
#include <aidl/android/hardware/radio/modem/HardwareConfig.h>
#include <aidl/android/hardware/radio/modem/ActivityStatsInfo.h>
#include <aidl/android/hardware/radio/modem/ImeiInfo.h>
#include <aidl/android/hardware/radio/modem/RadioState.h>

#include "qmi_modem_backend.h"

namespace aidl::android::hardware::radio::modem {

class RadioModemService final : public BnRadioModem {
public:
    RadioModemService();
    ~RadioModemService() override = default;

    ndk::ScopedAStatus enableModem(int serial, bool on) override;
    ndk::ScopedAStatus getBasebandVersion(int serial) override;
    ndk::ScopedAStatus getDeviceIdentity(int serial) override;
    ndk::ScopedAStatus getHardwareConfig(int serial) override;
    ndk::ScopedAStatus getModemActivityInfo(int serial) override;
    ndk::ScopedAStatus getModemStackStatus(int serial) override;
    ndk::ScopedAStatus getRadioCapability(int serial) override;

    ndk::ScopedAStatus nvReadItem(int serial, NvItem itemId) override;
    ndk::ScopedAStatus nvResetConfig(int serial, ResetNvType resetType) override;
    ndk::ScopedAStatus nvWriteCdmaPrl(
        int serial, const std::vector<uint8_t>& prl) override;
    ndk::ScopedAStatus nvWriteItem(int serial, const NvWriteItem& item) override;

    ndk::ScopedAStatus requestShutdown(int serial) override;
    ndk::ScopedAStatus responseAcknowledgement() override;

    ndk::ScopedAStatus sendDeviceState(
        int serial, DeviceStateType deviceStateType, bool state) override;

    ndk::ScopedAStatus setRadioCapability(
        int serial, const RadioCapability& rc) override;

    ndk::ScopedAStatus setRadioPower(
        int serial,
        bool powerOn,
        bool forEmergencyCall,
        bool preferredForEmergencyCall) override;

    ndk::ScopedAStatus setResponseFunctions(
        const std::shared_ptr<IRadioModemResponse>& radioModemResponse,
        const std::shared_ptr<IRadioModemIndication>& radioModemIndication) override;

    ndk::ScopedAStatus getImei(int serial) override;

private:
    RadioResponseInfo makeResponseInfo(
        int serial, ::aidl::android::hardware::radio::RadioError error) const;

    std::shared_ptr<IRadioModemResponse> getResponse();

    void sendSimpleError(
        int serial,
        ::aidl::android::hardware::radio::RadioError error,
        const char* operation);

    mutable std::mutex mutex_;
    std::shared_ptr<IRadioModemResponse> response_;
    std::shared_ptr<IRadioModemIndication> indication_;

    j5qmi::QmiModemBackend backend_;
    bool modemEnabled_ = true;
};

} // namespace aidl::android::hardware::radio::modem
