# BthAapBridge — KMDF Bluetooth L2CAP Profile Driver

Optional Windows x64 KMDF 1.15 Bluetooth profile driver enabling two-way Apple Accessory
Protocol (AAP) communication over BR/EDR L2CAP (PSM `0x1001`).

## Why a driver exists

Windows does not expose BR/EDR L2CAP client channels to user-mode applications (Winsock
`AF_BTH` only supports RFCOMM `BTHPROTO_RFCOMM`). AirPods advertise the AAP service over
SDP (`{74EC2172-0BAD-4D01-8F77-997B2BE0722A}`) on L2CAP PSM `0x1001`.

`BthAapBridge` binds only to that single PnP service PDO
(`BTHENUM\{74ec2172-0bad-4d01-8f77-997b2be0722a}`), leaving Microsoft's Bluetooth, A2DP,
HFP, and AVRCP stack completely untouched. It exposes `GUID_DEVINTERFACE_APB_AAP` so
`Core::AirPods::L2capControlTransport` can stream raw AAP frames via overlapped
`ReadFile` / `WriteFile`, and supports `IOCTL_APB_AAP_SET_LOW_LATENCY` to negotiate
local HCI guaranteed QoS (`CFG_QOS_LOCAL`) and bounded flush timeouts (`CFG_FLUSHTO`) on
the ACL link (with automatic fallback to standard MTU config if the host radio rejects
guaranteed QoS).

## Building (no Administrator rights required)

```powershell
.\Driver\BthAapBridge\Build-Driver.ps1
```

This fetches the official `Microsoft.Windows.WDK.x64` NuGet package into `Build/wdk-nuget/`
and builds `Build/driver/x64/BthAapBridge.sys`, `BthAapBridge.inf`, and `BthAapBridge.cat`.

## Installing for local development (Test Signing)

1. Enable Windows Test Signing once in an elevated prompt and reboot:
   ```powershell
   bcdedit /set testsigning on
   ```
2. Test-sign and install the driver in an elevated prompt:
   ```powershell
   .\Driver\BthAapBridge\Install-TestDriver.ps1
   ```
3. Launch `AirpodsBridge.exe` — `Settings -> About -> Diagnostics` will automatically switch
   from **BLE Broadcast** to **AAP L2CAP connected**, unlocking 1% battery precision, Noise
   Cancellation / Transparency / Adaptive Audio controls, and Conversation Awareness.

## Public distribution

For production releases without Test Signing, submit the CAB package containing
`BthAapBridge.sys`, `BthAapBridge.inf`, and `BthAapBridge.pdb` through the Microsoft
Partner Center **Hardware Attestation Signing** portal using an EV code-signing certificate.
