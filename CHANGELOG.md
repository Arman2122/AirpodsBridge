# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Ultra-Low-Latency Audio Engine (`3.0 ms` `IAudioClient3` + `IPolicyConfig` APO Bypass)**
  (`Source/Core/AudioControl_win.cpp`, `Source/Gui/SettingsWindow.cpp`):
  - Upgraded the wear-aware WASAPI stream in `AudioControl` to
    `IAudioClient3::InitializeSharedAudioStream` at the endpoint's minimum hardware period
    (`3.0 ms` on AirPods instead of Windows' `10.0 ms` default, with clean legacy fallback),
    dropping Windows' shared-mode audio mixing latency system-wide for every game and app while
    AirPods are worn.
  - Added live `IPolicyConfig::SetPropertyValue` (`PKEY_AudioEndpoint_Disable_SysFx`) APO
    bypass alongside native 64-bit registry writes, native COM WMI power policy, and USB
    Bluetooth Selective Suspend registry keys inside the zero-PowerShell native latency
    switcher.
- **Dedicated Health Settings Page & Compact Popup Heart Rate Pill Bar**
  (`Source/Gui/SettingsWindow.cpp`, `Source/Gui/Widget/HeartRateCard.h/.cpp`,
  `Source/Gui/MainWindow.cpp`, `Source/Presentation/MainWindowPresentation.h/.cpp`):
  - Moved the Heart Rate monitor out of **AirPods control** into a dedicated **Health**
    sidebar section with an enlarged Apple Health hero card, Heart Rate sensor toggle,
    live RTBuddy service telemetry readout (`RTBuddy Service <id> · Optical PPG 1 Hz`), and
    a toggle to show or hide the popup bar (`show_heart_rate_in_popup`, enabled by default).
  - Added a compact Liquid Glass `PopupHeartRateBar` inside the battery popup for optical
    heart-rate models (**AirPods Pro 3** and **Powerbeats Pro 2**) with a pulsing crimson
    heart, live tabular BPM, Heart Rate Zone pill, mini sparkline, one-click `Measure`/`Stop`
    pill button, and click-to-open **Settings → Health**.
- **Complete 31-Model Apple AirPods & Beats Support with Official Apple 3D Artwork**
  (`Source/Domain/Base.h`, `Source/Domain/AapProtocol.cpp`, `Tools/apple_device_assets.py`):
  - Added full Bluetooth PID decoding, Apple `0x1D` model-number fallback resolution
    (`ModelFromModelNumber`), hardware capability profiles, and genuine Apple 3D renders/loops
    from `mesu.apple.com` across all 31 AirPods and Beats models — including **AirPods 5**,
    **AirPods 5 (Wireless Case)**, **AirPods Max 2**, **Powerbeats Pro 2** (all 4 colours +
    60 fps loop + Heart Rate support), **Powerbeats Pro**, **Powerbeats**, **Powerbeats 3**,
    **Powerbeats Fit**, **BeatsX**, **Beats Flex**, **Beats Solo3**, **Beats Solo Pro**,
    **Beats Solo 4**, **Beats Solo Buds**, **Beats Studio3**, **Beats Studio Pro**, **Beats
    Studio Buds**, **Beats Studio Buds +**, **Beats Fit Pro**, and **Beats 360**.
  - Added white-matte HEVC-with-alpha un-premultiplication and straight-alpha studio backdrop
    handling in `Tools/apple_device_assets.py` for Apple's 2026 `SharingDeviceAssets` bundles.
- **Real AirPods Pro 3 Optical Heart Rate (BPM) Engine** (`Source/Domain/RtBuddySensor.h/.cpp`,
  `Source/Core/AirPods.cpp`, `Source/Gui/Widget/HeartRateCard.h/.cpp`):
  - Full Apple RTBuddy (`opcode 0x17`, `SensorDataWX`) optical PPG heart-rate protocol: opens
    RTBuddy services 0 & 4, powers on `HRM_STATE` (`0x30`), queries dynamic HID sensor
    descriptors (`VendorID 1452`, `HeartRate` layout) with legacy service fallback (`0x54` /
    `0x13` / `0x14`), reassembles split frames, filters warm-up optical samples (`confidence >=
    0x80` and hardware-verified status tails), runs a 12 s watchdog with up to 3 automatic
    retries, and auto-pauses when buds leave your ears to save battery.
  - Upgraded Apple Health `HeartRateCard` with a `FrameClock`-driven lub-dub pulsing heart
    synced to live BPM (0% idle CPU when hidden or inactive), Heart Rate Zone pill (`Resting`,
    `Normal`, `Elevated`, `Cardio`, `Peak`), optical PPG signal quality percentage, `Min · Avg ·
    Max` range, Y-axis scale ticks, and live locking/acquiring feedback.
- **Conversation Awareness Smooth Windows Media Ducking** (`Source/Core/AudioControl_win.cpp`,
  `Source/Core/AirPods.cpp`):
  - Smoothly lowers Windows playback volume when AirPods detect you speaking (`0x4B` levels
    `1..2`), steps volume back up smoothly on recovery levels (`3..7`) with a 2 s quiet-restore
    fallback, and immediately respects manual Windows volume slider adjustments.
- **Apple iOS 26/27 Liquid Glass UI & Exact Apple HIG Geometry** (`Source/Gui/MainWindow.cpp`,
  `Source/Gui/Widget/AppleNoiseControl.cpp`, `Source/Gui/Widget/PushButton.cpp`,
  `Source/Gui/Widget/HeartRateCard.cpp`):
  - True 28 px continuous squircle popup card with translucent soft ambient shadow, 1 px
    Liquid Glass specular rim, mathematically concentric 28×28 corner buttons (`28 - 14 = 14`
    px radius), and full stadium pill primary buttons (`radius = height / 2`).
  - Interactive iOS 27 Liquid Glass Listening Mode slider (5-landmark stadium capsule:
    Transparency dotted-halo, dotted ring, Adaptive dual-sparkle inside nested inner glass
    pill, solid ring, and Noise Cancellation arch) supporting both click and smooth
    FrameClock-animated drag, paired with circular Liquid Glass Control Center quick
    buttons below (Conversation Awareness, Hearing Aid / Personalized Volume, Listening Off).
  - Upgraded **Settings -> AirPods control** with the live iOS 27 Listening Mode slider and a
    new **Health & sensors** card featuring an AirPods Pro 3 Heart Rate sensor toggle, live
    tabular BPM + Min/Max readout, and a custom-painted Apple Health red cubic-Bezier
    sparkline (plus inline `♥ BPM` badge in the popup).
- **Instant Connect & Automatic Audio Output Switch / Restore** (`Source/Core/QuickConnect_win.cpp`,
  `Source/Core/AudioControl_win.cpp`, `Source/App/Application.cpp`, Settings -> **Audio &
  shortcuts**):
  - Cached paired Bluetooth device enumeration and A2DP-first reconnection for noticeably
    faster one-tap Connect with immediate `"Connecting..."` button feedback.
  - **Switch audio on connect** (`auto_switch_audio_on_connect`, enabled by default)
    automatically switches Windows playback output to AirPods Stereo when connected and
    restores your previous headset or speaker output when AirPods disconnect.
- **No-Test-Mode Bluetooth Radio Proxy Service** (`Service/`, `Docs/RadioService.md`,
  `Tools/Setup-RadioService.ps1`, Settings -> **AirPods control**):
  - Native x64 C++20 Windows Service (`AirpodsBridgeService.exe`, zero .NET/Qt runtime
    dependencies) that unlocks full two-way AAP AirPods controls while Windows Test Mode
    stays OFF and Secure Boot stays ON by combining Windows' inbox `WinUSB` driver,
    Microsoft-signed `usbip-win2`, and a user-mode Bluetooth HCI/L2CAP MITM proxy.
  - Pure C++20 protocol engine (`Service/Protocol/`): USB/IP v1.1.1 wire codec with
    `usbip-win2` front-packed isochronous compaction, HCI/L2CAP PSM `0x1001` channel
    injection/hiding with exact `Number_Of_Completed_Packets` credit accounting and L2CAP
    PDU fragmentation safety, `HCI_QoS_Setup` 3.75 ms low-latency polling rewrite,
    optional HCI-level RFCOMM/HFP decline for tracked AirPods, and a virtual USB Audio
    Class 1.0 (UAC1) 48 kHz mono microphone emulator with adaptive clock-drift jitter
    buffering, PLC, DC-block high-pass, and soft limiter.
  - Circuit-breaker safety watchdog (`WatchdogPolicy`): automatically restores the native
    vendor Bluetooth driver after 3 takeover failures in 10 minutes, plus one-command
    `--restore-radio` recovery and read-only `--preflight` hardware inspection.
  - `ServiceControlTransport` + `CompositeControlTransport` (`Source/Core/`) seamlessly
    preferring the No-Test-Mode Radio Service with automatic fallback to `BthAapBridge`
    KMDF or zero-CPU BLE broadcast mode.
  - Expanded AAP codec (`Source/Domain/AapProtocol.h`) and dedicated **AirPods control**
    Settings page covering Noise Control, Conversation Awareness (+ live speech state),
    Adaptive Audio level, One-Bud ANC, Personalized Volume, Loud Sound Reduction, Allow
    "Off" listening mode, Microphone Side, Stem Press Speed, Stem Press & Hold Duration,
    Tone Volume, Head Gestures, Hearing Aid, Media Assist, Heart Rate telemetry (AirPods
    Pro 3), and AACP sideband mic commands.
  - Native silent elevated setup (`Service/Win/NativeSetup.cpp`) invoked directly via UAC
    (`SW_HIDE`, no PowerShell window shown to users), streaming live progress into an
    in-app progress bar in Settings and the new Apple-style first-run Welcome dialog
    (`Source/Gui/WelcomeDialog.cpp`) where the Control Engine, Nearby Popup, and Auto-Start
    are enabled by default and reversible anytime in Settings.
- **Apple Control Center popup & Nearby Other-Device detection** (`Source/Gui/MainWindow.cpp`,
  `Source/Gui/Widget/AppleNoiseControl.cpp`, `Source/Core/AirPods.cpp`):
  - Authentic Apple-style Noise Control capsule inside the popup with custom vector icons
    (Off, ANC, Transparency, Adaptive), FrameClock-driven animated sliding pill, quick
    feature pills (Conversation Awareness with live speaking indicator, Hearing Aid /
    Personalized Volume, live Heart Rate BPM on AirPods Pro 3), and one-click Controls jump.
  - Nearby popup works even when AirPods are connected to another device (such as an iPhone
    or iPad), displaying 3D Apple artwork, live Left/Right/Case battery rings, status badge,
    and an Apple-blue one-click Connect button (`show_nearby_popup_when_disconnected`,
    enabled by default) with hover-safe auto-hide and dismiss cooldown.
- Future-proof Apple Accessory Protocol (AAP) two-way control stack over BR/EDR L2CAP:
  - Pure C++20 codec (`Source/Domain/AapProtocol.h`, `Tests/AapProtocolTests.cpp`) for 1%
    battery, ear detection, Noise Cancellation / Transparency / Adaptive Audio switching,
    Conversation Awareness, One-Bud ANC, Personalized Volume, Loud Sound Reduction, Adaptive
    Noise Level, Head Gestures, rename, and device metadata.
  - Real Windows KMDF 1.15 x64 L2CAP profile driver (`Driver/BthAapBridge/`) binding to
    `BTHENUM\{74ec2172-0bad-4d01-8f77-997b2be0722a}` (PSM `0x1001`) with reproducible NuGet
    WDK build and test-signing scripts, paired with `L2capControlTransport`
    (`Source/Core/AirPodsTransport_win.cpp`) using `CM_Register_Notification` for zero idle
    cost when not installed.
  - Capability-gated popup segmented control and tray submenu (including Conversation
    Awareness) that automatically activate whenever an AAP transport connects.
- Event-driven WASAPI Audio Protection & Smart Routing (`Source/Core/AudioControl.h`,
  Settings -> **Audio & shortcuts**):
  - Detects active Hands-Free Profile (HFP) sessions and shows which process triggered
    Hands-Free audio directly in the popup (`Hands-Free (Discord.exe)`).
  - **Preferred microphone** lock keeps Windows' default capture/communications mic on a
    chosen desktop microphone so calls and games do not degrade AirPods to 16 kHz mono HFP.
  - **Switch output when worn** (`SmartAudioRoutePolicy`) switches Windows playback to
    AirPods A2DP Stereo when put in your ears and restores your previous output on removal.
  - **Limit reconnect volume** caps AirPods playback volume via `IAudioEndpointVolume`.
- **Low-latency & gaming** audio suite (Settings -> **Audio & shortcuts**,
  `Tools/Set-AirPodsLatencyProfile.ps1`, `Driver/BthAapBridge`):
  - Opt-in wear-aware event-driven WASAPI silent A2DP keep-alive (`audio_keep_alive`,
    MMCSS `Pro Audio` scheduling, `AUDCLNT_BUFFERFLAGS_SILENT`) that eliminates A2DP
    suspend/wake-up audio lag while worn and releases the stream automatically when AirPods
    are taken out or disconnected.
  - Live Bluetooth A2DP codec (`AAC` vs `SBC`), Audio Enhancements (`Disable_SysFx`), and
    WASAPI shared-mode engine period telemetry in Settings, Diagnostics, and `--status` JSON.
  - One-click UAC-consented **Gaming mode (SBC)** / **Music mode (AAC)** switcher that
    toggles `BthA2dp\Parameters\BluetoothAacEnable`, endpoint `Disable_SysFx`, and USB
    Bluetooth power-saving suspend, then re-negotiates the AirPods A2DP link without
    rebooting.
  - `IOCTL_APB_AAP_SET_LOW_LATENCY` in `BthAapBridge.sys` configuring L2CAP `CFG_FLUSHTO`
    and `CFG_QOS_LOCAL` (10 ms guaranteed local QoS with automatic standard-MTU fallback).
- Native Win32 system-wide **Global shortcuts** (`Source/Core/GlobalHotkeys.h`) for
  connect/disconnect, battery popup toggle, and Noise Control cycling.
- Per-user Named Pipe **CLI automation interface** (`Source/Core/Ipc.h`):
  `AirpodsBridge.exe --status`, `--connect`, `--disconnect`, `--toggle-connect`, `--popup`,
  `--settings`, and `--mode <off|anc|transparency|adaptive|cycle>` for Stream Deck,
  AutoHotkey, PowerToys, and terminal scripting.
- Pure-domain **Battery Intelligence** (`Source/Domain/BatteryTracker.h`,
  `FullChargeAlertPolicy`): observed listening-time estimates (`~3h 40m left`), sustained
  asymmetric drain / dirty charging-pin warning, and optional 100% full-charge notification.
- Zero-BLE PnP model recognition (`Core::AirPods::ModelFromProductId`): smart bind picker
  sorts and labels paired Apple models (`VID 0x004C`), and the disconnected popup renders
  the bound model's artwork alongside the **Connect** button.
- Modern Windows toast notifications (`Source/Core/Notifications.h`) with automatic tray
  balloon fallback in portable mode, plus a live **Diagnostics** card with 1-click **Copy
  report** in Settings -> About.
- One-click **Connect / Disconnect** toggle in the tray context menu (using
  `KSPROPERTY_ONESHOT_DISCONNECT` alongside `KSPROPERTY_ONESHOT_RECONNECT`) and a
  **Connect** action button directly in the popup when the bound device is disconnected.
- Automatic Quick Connect device linking on bind (using `System.Devices.Aep.ContainerId`),
  bound device name readout and direct **Bind / Change** control in Settings -> General.
- Manual **Check now** update button with live status feedback in Settings -> About.
- True portable mode: placing `AirpodsBridge.ini` or a `portable` marker next to the
  executable stores settings in INI format and logs locally, and routes updates to the
  release page instead of launching the per-user installer.
- Power and shell lifecycle handling (`Source/Core/SystemEvents.h`): pauses BLE scanning
  across Windows sleep/Modern Standby, reconciles device state on wake, and re-attaches the
  taskbar battery widget if `explorer.exe` restarts.
- Launching a second instance of AirpodsBridge while it is already running now surfaces the
  battery popup instead of exiting silently.

### Changed

- BLE watcher now pushes Apple company-ID (`0x004C`) filtering down to the Windows Bluetooth
  stack in release builds and uses passive scanning, preventing unrelated nearby BLE devices
  from waking the process.
- Replaced the three always-running timer threads in `AirPodsStateManager` with deterministic
  timestamp expiry driven by a single lazy timer armed only while live BLE state exists.
- Rewrote `Core::AppleCP::AirPods` parsing to walk compound Apple Continuity TLV payloads
  with explicit bounds checks and zero pointer-casting UB, and removed the last
  `Domain -> Core` header layering exception (`Source/Domain/BluetoothTypes.h`).
- Tray icons now render crisp multi-DPI bitmaps (`16`–`64` px) with semantic battery badge
  colours (green when healthy, red only when low) and a clean accent update indicator;
  tooltips are safely clamped within Windows' 127-character limit.
- Taskbar status widget now follows Windows' taskbar theme (`SystemUsesLightTheme`) so text
  and icons remain readable on light taskbars.
- Global media automation now runs on a dedicated lazy MTA worker thread, pauses all active
  SMTC sessions rather than only the single "current" session, and expires stale resume
  state after 30 minutes.
- Updater checks every 6 hours using `User-Agent`, request timeouts, cached prerelease
  detection, and `ETag`/`If-None-Match` conditional requests; failing or cancelling a
  download no longer exits the running application.
- Logs now rotate (5 MB x 2 files) and flush on warnings/errors plus every 3 seconds rather
  than growing unboundedly or flushing synchronously per trace line.
- NSIS installer now replaces a previous per-user copy only after confirming installation,
  attempts graceful shutdown before forced termination, and cleans up the startup registry
  entry on uninstall; CI caches vcpkg binaries and signs `AirpodsBridge.exe` before
  packaging.

### Fixed

- Fixed phantom `DEVICE_STATE_NOTPRESENT` endpoints from earlier Bluetooth bus instances
  breaking AirPods audio routing and causing 8-second reconnect drops (`Source/Core/AudioControl_win.cpp`,
  `Source/Core/QuickConnect_win.cpp`, `Service/Protocol/HciProxyEngine.cpp`):
  - `AudioControl::RebindTrackedEndpoints` now enumerates only live endpoints (`ACTIVE` /
    `UNPLUGGED`) with strict state ranking (`ACTIVE > UNPLUGGED`) so dead phantom endpoints can
    never overwrite `_stereoActive` or silence playback/3 ms keep-alive after a radio restart.
  - `QuickConnect` now filters out `NOTPRESENT` phantom endpoints and strictly prioritizes the
    live **A2DP Stereo (`Headphones`)** endpoint on reconnect without poking `Hands-Free`
    (`BTAGService`) concurrently, eliminating HFP-only `avdtp=0` 8-second `reason=22` drops.
  - Kept `HCI_QoS_Setup` (`0x0807`) 100% Bluetooth Core Spec compliant (tightening poll latency
    to `3.75 ms` while preserving `Service_Type` and `Delay_Variation`) and deferred
    non-AVDTP AAP fallback to `3.5 s` with single-retry ceiling so hidden AAP never collides
    with Windows' initial A2DP setup.
- Fixed HCI ACL fragment-interleaving race condition in `AirpodsBridgeService`
  (`Service/Protocol/HciProxyEngine.cpp`, `Service/Win/RadioBridgeSession.cpp`) that could
  inject hidden AAP packets between fragments of a Windows A2DP audio frame (causing audio
  noise/glitches and ACL timeouts): switched injected ACL writes to single-owner pull mode on
  the Bulk OUT pipe worker so packets are pulled strictly at L2CAP PDU boundaries right before
  `WinUsb_WritePipe`, with automatic controller credit rollback on write errors and stale retry
  queue pruning.
- Fixed double elevated latency switcher launches and overlapping Bluetooth reconnect pulses
  (`Source/Core/AudioControl_win.cpp`, `Source/App/Application.cpp`,
  `Source/Gui/SettingsWindow.cpp`) via an atomic in-flight lock (`applyingProfile`), graceful
  WASAPI keep-alive drain prior to disconnect, and a single-epoch serialized reconnect timer.
- Fixed blank popup void between connection and initial battery frame, prevented BLE scanner
  restarts during radio reloads from resetting connected tray/taskbar state, and added RTBuddy
  Service 20 (`0x14`) + post-connect descriptor discovery so AirPods Pro 3 optical Heart Rate
  relocks reliably after every reconnect.
- Fixed clipped `"irPods Pro"` device title in `Source/Domain/AapProtocol.cpp` (real Apple
  `0x1D` metadata strings begin at byte offset 11 after the 5-byte metadata header).
- Fixed Conversation Awareness stuck displaying green `"Speaking"` even when turned off by
  decoding real `0x4B` byte 9 ducking levels instead of constant byte 8 (`0x01`), clearing
  speaking state on disable/ear-removal, and gating the green indicator on active Conversation
  Awareness.
- Fixed **Gaming mode (SBC)** / **Music mode (AAC)** failing with
  `C:\WINDOWS\Sysnative\WindowsPowerShell\v1.0\powershell.exe — The specified path does not
  exist` by replacing the PowerShell switcher completely with a pure native Win32 C++ elevated
  worker (`AirpodsBridge.exe --apply-latency-profile`, using native 64-bit `KEY_WOW64_64KEY`
  registry access, native COM WMI `MSPower_DeviceEnable`, `cfgmgr32.dll` A2DP node cycling, and
  native Bluetooth reconnect fallback) — zero PowerShell or external scripts required across
  x86, x64, and ARM64 Windows.
- Fixed the **Listening Off** button bouncing back to Transparency/Adaptive on AirPods Pro 2,
  AirPods Pro 3, AirPods 4 (ANC), and AirPods 5 by proactively arming `Allow "Off" Listening
  Mode` (`0x34`) on AAP connect, giving `0x34` a 160 ms firmware settling window before sending
  `0x0D = Off`, and performing an automatic one-shot `0x34` + `0x1A` cycle-mask retry if the
  buds reject Off during transition.
- Fixed a `0xC0000409` crash (`std::terminate` on recursive `std::mutex` lock) when changing
  AAP listening modes or toggles while Settings was open by ensuring `Core::AirPods::Manager`
  never emits Qt signals while holding `_mutex`.
- Fixed AAP L2CAP connections failing to open on subsequent reconnects after the first session
  by deferring hidden `PSM 0x1001` setup until Windows' `AVDTP` signaling channel settles
  (with 1.2 s fallback + 3 s handshake stall retry) and logging HCI disconnect reason codes.
- Fixed rapid Play/Pause and audio-output switching storms caused by BLE ear-detection jitter
  (1.2 s BLE wear debounce; instant for authoritative AAP) and suppressed immediate
  post-disconnect auto-connect loops.
- Fixed Quick Connect failing to wake A2DP Stereo when HFP render enumerated first, completed
  pending requests immediately when WinRT confirms Bluetooth connection, and replaced spammy
  Windows notifications overlapping the popup with clean inline status feedback.
- Fixed Disable/Enable Radio Service leaving stale WinRT device handles or cached Quick Connect
  endpoints by cleanly rebinding Bluetooth handles and re-pushing proxy options after the
  adapter switches.
- Improved exact 1% AAP battery decoding for Left, Right, and Charging Case (accepting `0x00`
  not-charging and `0x7F` unavailable encodings, and preserving the last known 1% case level
  when the case lid closes).
- Fixed **Settings -> AirPods control** toggles staying disabled after the Radio Proxy Service
  connects: `StateManager::SetAapConnected` now seeds initial controllable state for the bound
  model immediately and `ControlConnectionChanged` refreshes the Settings page live.
- Fixed AAP toggles (such as Conversation Awareness) requiring multiple clicks or sending
  stale values by applying optimistic local state updates on every outbound AAP control frame
  and accepting both `0x00` and `0x02` wire false encodings.
- Fixed false `"Nearby · Case open"` badges while buds are worn out of the case (requiring
  `isBothPodsInCase` or an available case battery, and tracking the Continuity lid-open
  counter for reliable case-flip popups).
- Added automatic L2CAP `ConnReq` retry in `HciProxyEngine` on transient `0x0003` (Security
  Block) / `0x0004` (No Resources) rejections.
- Fixed a crash when selecting the pre-highlighted first device in `SelectWindow` with
  multiple paired AirPods (`setCurrentRow(0)` instead of `setSelected(true)`).
- Fixed a potential double-free / use-after-free when closing the popup window (`Alt+F4` /
  `WM_CLOSE`) by removing `Qt::WA_DeleteOnClose` from the `unique_ptr`-owned `MainWindow`.
- Fixed a 10x unit mismatch in `AirPodsStateManager` that falsely rejected valid random
  Bluetooth address rotations whenever battery changed by one 10% step.
- Fixed left-only BLE advertisements dropping `isLidOpened` and `isBothPodsInCase` flags
  when case battery was unavailable.
- Fixed one-piece devices (AirPods Max) firing duplicate Left/Right low-battery alerts and
  requiring both sides for ear detection.
- Fixed `DownloadInstall` and `WriteStackTraceFile` failing on Windows accounts with
  non-ASCII usernames by using wide `std::filesystem::path` instead of ANSI narrow paths.
- Fixed BLE watcher shutdown races by revoking WinRT event tokens, life-guarding callbacks,
  and moving retry backoff off the WinRT callback thread; hardened `FrameClock` exit
  teardown against detached-thread UAF.
- Fixed `TaskbarStatus` leaking a foreign `QWindow` on every enable cycle and mis-sizing
  ring icons when `setShowText(false)` followed `setBatterySize(...)`.
- Fixed popup battery rings ignoring user-configured `low_battery_threshold`, case/brace
  mismatches in `QuickConnect::Controller`, a missing `std::format` call in `AirPods.cpp`,
  and a leaked `DECLARE_META_FIELD` macro in `Settings.h`.

## [1.0.0] - 2026-09-07

First public release.

### Added

- Battery levels for each bud and the charging case, shown in a tray popup and optionally
  on the tray icon or the taskbar.
- Automatic ear detection: media pauses when a bud is removed and resumes when it is
  returned.
- Quick connect, so one click on the tray icon connects the bound device.
- In-app update checking against GitHub Releases.
- An NSIS installer and a portable ZIP, published with SHA-256 checksums, and optional
  Authenticode signing when repository secrets are configured.
- Three options. *Check for updates automatically* (on by default) stops the hourly
  GitHub request when turned off. *Show the popup when the case opens* (on) makes the
  lid-open popup a choice. *Resume playback when both are in* (on) lets ear detection
  pause without ever resuming, for anyone who found the resume surprising.
- Four behaviours on the Features page. *Pause when AirPods disconnect* (on by default)
  stops media carrying on through the speakers. *Low battery notification* (on, at 20 %)
  shows a notification once per drop, and its level — 10, 20 or 30 % — also drives the
  tray icon's "when low" option. *One AirPod is enough* (off) makes ear detection follow
  the last bud rather than both. *Connect when the case opens* (off) connects the quick
  connect device when its case opens nearby; it keeps Bluetooth scanning on while
  disconnected and says so.
- The popup draws the device with Apple's own artwork: the render and loop an iPhone shows
  on its proximity card, at Apple's own 60 fps, transparent, in the device's own colour
  where Apple publishes it (every AirPods Max and Beats Fit Pro colour). `Tools/apple_device_assets.py` fetches the
  bundles from Apple's asset catalogue, decodes the HEVC-with-alpha loops by splitting
  their layers, and writes `Source/Resource/Device/`; `check` reports when Apple has
  published newer artwork. See Docs/Artwork.md.
- A notification once the installer has launched the application, saying that it is
  running in the system tray and what the icon does. `--after-install` shows it; the
  installer's finish page passes that. The first run shows the same one.
- AirPods Max, and the other one-piece devices, show one battery: one ring in the popup,
  one line in the tray tooltip, one figure on the taskbar, with no left, right or case.
  The advertisement carries the level on one side, and the application reads it from
  whichever side has it.
- Dark mode. *Theme* on the General page is System, Light or Dark; System follows the
  mode Windows is set to and changes with it while the application runs, without a
  restart. The popup, the settings window, the dialogs, the tray menu and the title bars
  all follow. The dark palette is designed rather than inverted, and
  `Tests/ThemeTests.cpp` holds both palettes to WCAG contrast on every surface text is
  drawn on.
- `Tests/AutomationTests.cpp` drives every one of those rules through its transitions
  without hardware, and `Tests/SettingsPreview.cpp` renders the settings window to PNG so
  it can be looked at without a tray icon or a Bluetooth adapter.
- Animations run at the display's refresh rate, on any display. The scroll glide, the
  switches, the popup sliding in and the device loop are advanced once per vertical blank
  of the display they are on — 60, 120, 144, 240 or 300 Hz — rather than from Qt's fixed
  60 Hz timer, and stop costing anything the moment nothing moves. `Gui::FrameClock`
  (`Source/Gui/FrameClock.h`) is the one clock; there is no setting, because there is
  nothing to choose.

### Changed

- Settings changes are subscriptions. `Core::Settings::Watch` replaces the `ApplyObserver`
  interface that grew a method per setting; `ApbApplication::WireSettings` is now the
  complete list of what settings drive, one line each, and watchers run after the settings
  lock is released.
- The decisions behind the Features page are policies in
  `Source/Domain/AutomationPolicies.h`, carried out by `Core::Automation::Engine` through an
  `Effects` interface. The device manager reports and no longer decides.
- The update checker is owned by the application rather than the main window.
- The application's stock widgets -- message boxes, the device chooser, the update
  download window, the tray menu -- are drawn by Qt's Fusion style in both modes, from the
  application's own palette, instead of by the Windows style. The Windows style in Qt 5
  paints buttons and menus through the system theme and cannot go dark.
- The bundled AVI animations and Qt Multimedia are gone: the popup paints animated WebP
  frames through `Widget::DeviceView`, which decodes nothing while the popup is hidden and
  shows a still when Windows' animation setting is off. The installer no longer ships the
  Qt Multimedia libraries or media plugins.
- The installer is rebuilt on a custom NSIS template: it installs for the current user
  under `%LOCALAPPDATA%\Programs` with no administrator prompt, has branded, DPI-aware
  pages, offers launch and a desktop shortcut on the finish page, closes a running copy
  before replacing files, replaces a previous version in place, and registers with Apps &
  features properly. A copy installed for all users by the 1.0.0 installer is detected and
  its uninstaller offered.
- The quick connect device is chosen once and serves both the tray click and auto-connect.
- The popup's battery rings sit under what they measure. Apple composes each model's
  picture differently, so the artwork catalogue now records where the buds and the case
  are and the rings are placed from that, instead of at one fixed position.
- The mouse wheel glides through the settings window instead of jumping a step per notch,
  and a scroll over the range slider or the device list no longer changes the setting
  under the pointer.
- The settings window is rebuilt. Pages are chosen from a sidebar instead of tabs; every
  setting is a row with its explanation under the title rather than behind a hover tip;
  on/off settings are switches, three-way choices are segmented controls, and the
  Bluetooth range slider shows the value it is set to. It uses the same type scale,
  colours and continuous corners as the popup.
- Restore defaults asks first, and keeps the bound AirPods and the quick connect device
  rather than forgetting them along with the preferences.
- The quick connect device list is greyed out until quick connect itself is on, like
  every other dependent option; it used to stay active regardless.
- The battery indicator is now a ring rather than a horizontal cell, matching how iOS
  shows the charge of a connected accessory: a full-circle track with an arc over it
  sweeping clockwise from twelve o'clock. Colours are Apple's system green and red on a
  systemGray5 track, and every measurement derives from the diameter, so the shape holds
  at any size.
- The charging bolt sits in the hollow of the ring, where iOS puts it.
- The close button's cross is inset proportionally and stroked thin with round caps,
  rather than a fixed 3px square-capped cross.

- Interface type is now Inter, bundled under the SIL Open Font License, with SF Pro
  preferred when the machine already has it installed. Sizes, weights and tracking come
  from one type scale rather than being pinned per-widget in the `.ui` forms.
- The action button draws a continuous (superellipse) corner rather than a circular one.
  A stylesheet `border-radius` can only produce a circular corner, so the button paints
  itself; see `Source/Gui/Painting.h`.

- The popup's corners are now rounded by the desktop compositor instead of by clipping the
  window to a region. The old approach used a one-bit mask, which cannot antialias, so
  every curve came out stair-stepped and the window also lost its native drop shadow.
  Windows 10, which has no such attribute, now leaves the window square rather than
  falling back to a mask.
- Window animation length is derived from the display's refresh rate and rounded to a whole
  number of frames, rather than a fixed 500 ms that matched no common refresh rate. It
  collapses to zero when "Show animations in Windows" is turned off.
- Colours and metrics moved out of the widgets and the `.ui` forms into a single token
  header, `Source/Presentation/Theme.h`. The palette stays deliberately light, because the
  bundled device animations are composited onto white.

### Fixed

- Hover and press states on the close button repainted synchronously on every mouse event,
  which caused flicker. They now coalesce into the next paint.
- The close button's circle was drawn to the integer widget rect, so its antialiased edge
  was clipped by the widget boundary and left a hard fringe.

[Unreleased]: https://github.com/Arman2122/AirpodsBridge/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Arman2122/AirpodsBridge/releases/tag/v1.0.0
