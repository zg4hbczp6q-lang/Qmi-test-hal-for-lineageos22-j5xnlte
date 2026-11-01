# j5xnlte-qmi-radio

Experimental QMI radio stack for Samsung Galaxy J5 2016 (`j5xnlte`, Qualcomm MSM8916).

## Target

- Android platform baseline: Android 15 / API 35
- Runtime architecture: `arm64-v8a`
- Modem transport: kernel RPMSG WWAN control port (`/dev/wwan0qmi0`)
- Known modem interfaces: `/dev/wwan0at0`, `/dev/wwan0at1`, `/dev/wwan0qmi0`

## Current status

**Bootstrap stage only. This is not an installable Radio HAL or KernelSU module.**

The first CI milestone builds a small, hardware-independent QMUX framing library for
Android arm64 and runs host-side parser tests. It does not open the modem, transmit
packets, register Android Binder services, or change device configuration.

This is intentional: protocol framing and tests must be reliable before implementing
QMI transactions and the Android Radio AIDL service.

## Build with GitHub Actions

1. Create a GitHub repository named `j5xnlte-qmi-radio`.
2. Upload/push these files to its default branch.
3. Open **Actions** and run **Build QMI core** (or push a commit).
4. Download the `j5xnlte-qmi-core-android15-arm64` artifact.

The workflow:
- compiles and tests the QMUX parser on Ubuntu;
- cross-compiles a static library for Android 15 `arm64-v8a` using NDK r27c;
- uploads the static library as a workflow artifact.

## Development roadmap

1. QMUX framing, stream I/O, transaction tracking and unit tests.
2. QMI CTL client allocation and service discovery.
3. DMS identity and modem state.
4. NAS registration, radio technology and signal reporting.
5. UIM/SIM state and PIN operations.
6. WDS data sessions and Linux `rmnet` integration.
7. WMS messaging and voice support where the modem firmware exposes the required services.
8. Android 15 Stable AIDL Radio HAL (`IRadioModem`, `IRadioSim`, `IRadioNetwork`,
   `IRadioData`, `IRadioMessaging`, `IRadioVoice`, `IRadioConfig`), VINTF and SELinux.
9. Device-side testing and a reversible KernelSU installer after the HAL is functional.

## Important constraints

- Do not write to EFS, modemst, NV items or IMEI.
- Do not change modem firmware.
- Do not assume that AT commands supported by another Qualcomm modem are supported here.
- The lack of a SIM during initial diagnostics means network registration cannot yet be
  validated.
- No flashable ZIP is produced at this stage.

## References

- Android Stable AIDL: https://source.android.com/docs/core/architecture/aidl/stable-aidl
- AIDL HALs and VINTF: https://source.android.com/docs/core/architecture/aidl/aidl-hals
- Radio AIDL source: https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/android15-release/radio/aidl/
- GloDroid mm-radio (architectural reference; uses ModemManager and is not a drop-in QMI HAL):
  https://github.com/GloDroid/mm-radio
