#include "radio_modem_service.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>

using aidl::android::hardware::radio::modem::RadioModemService;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(4);

    auto service = ndk::SharedRefBase::make<RadioModemService>();

    const std::string instance =
        std::string() +
        RadioModemService::descriptor +
        "/slot1";

    const binder_status_t status =
        AServiceManager_addService(service->asBinder().get(), instance.c_str());

    if (status != STATUS_OK) {
        LOG(ERROR) << "Failed to register RadioModemService: " << status;
        return 1;
    }

    LOG(INFO) << "Registered " << instance;

    ABinderProcess_joinThreadPool();
    return 1;
}
