# Performance

AirpodsBridge is meant to sit in the notification area all day. Background CPU usage is
therefore treated as a product constraint, not just a benchmark number: an idle tray
application that keeps a core busy is a bug, however good its features are.

## The budget

While idle — bound device connected, window closed — the process should be indistinguishable
from doing nothing. In practice that means well under 0.1% of total CPU, or a fraction of a
second of process CPU time over a 60-second window.

An earlier revision of the lifecycle code cost roughly twenty times that. The techniques
below are what closed the gap, and every one of them is still in the code:

| Technique | Where |
| --- | --- |
| BLE scanning follows the bound device's connection state, uses passive scanning with an OS-level `0x004C` Apple manufacturer-data filter, and suspends automatically when a push-based AAP transport is connected | `Source/Core/Bluetooth_win.cpp`, `Source/Core/AirPods.cpp`, `Source/Domain/AppleCP.cpp` |
| Scanning while the device is disconnected is opt-in, and nothing seen then reaches the state manager or the popup | `Source/Core/AirPods.cpp` (`SetScanWhileDisconnected`) |
| Device state expiry uses a single lazy timer armed only while live BLE state exists -- zero background threads or wakeups when idle | `Source/Domain/AirPodsStateManager.cpp`, `Source/Core/AirPods.cpp` |
| Media playback automation runs on a lazy MTA worker thread that sleeps on a condition variable when idle, never blocking Qt's STA GUI thread | `Source/Core/GlobalMedia_win.cpp` |
| Rotating file logs flush on warnings/errors and every 3 seconds rather than flushing synchronously on every trace line | `Source/Support/Logger.cpp` |
| The popup's device animation decodes only while the popup is visible, one frame at a time, never cached, and is paced by the frame clock, so a slow machine drops frames rather than slowing the loop | `Source/Gui/Widget/DeviceView.cpp` |
| Every animation advances from the display's vertical blank, waited on by one thread that sleeps on a condition variable while nothing moves; no animation timer runs in an idle application, and no frame is drawn that the display will not show | `Source/Gui/FrameClock.cpp`, `Source/Core/DisplayTiming_win.cpp` |
| Tray icons are reused until the rendered battery state actually changes | `Source/Gui/TrayIcon.cpp` |
| Taskbar geometry polling slows down after its initial positioning pass | `Source/Gui/TaskbarStatus.cpp` |
| Windows' light/dark switches (both app mode and taskbar mode) and sleep/wake / Explorer-restart broadcasts are heard through hidden message windows, never polled | `Source/Core/SystemTheme_win.cpp`, `Source/Core/SystemEvents_win.cpp`, `Source/Gui/ThemeController.cpp` |

The tray-icon cache is the easiest of these to break: `TrayIcon` compares an
`IconRenderState` and re-renders only on a difference. Adding a field that changes on every
tick — a timestamp, an animation frame — silently reintroduces continuous repainting.

One setting spends idle CPU on purpose: *Connect when the case opens*. It keeps the BLE
watcher running while the bound device is disconnected, because the lid-open advertisement
cannot be seen any other way. It is off by default and its description says what it costs.
Measure with it both ways; the budget above is for the default.

A second opt-in setting, *Keep audio stream awake* (`audio_keep_alive`, off by default),
opens an event-driven WASAPI shared-mode stream writing `AUDCLNT_BUFFERFLAGS_SILENT` frames
only while at least one AirPod is in an ear and the A2DP Stereo endpoint is active. It
eliminates Bluetooth A2DP suspend/wake-up lag for gaming and UI sounds, and closes the
stream immediately when AirPods leave your ears or disconnect so idle CPU returns to 0%.

## Measuring

There is no automated benchmark. To compare two builds by hand:

1. Build both in Win32 `RelWithDebInfo`. A `Debug` build is not representative.
2. Give each the same conditions: same bound device, same Bluetooth state, same taskbar
   configuration.
3. Let startup and device discovery settle before sampling.
4. Sample process CPU time over a fixed window:

   ```powershell
   $before = (Get-Process AirpodsBridge).TotalProcessorTime
   Start-Sleep -Seconds 60
   $after = (Get-Process AirpodsBridge).TotalProcessorTime
   ($after - $before).TotalSeconds
   ```

Divide by `60 * $env:NUMBER_OF_PROCESSORS` for a percentage of total CPU.

Results move with hardware, logical processor count, Bluetooth activity, audio devices and
which settings are enabled. Treat a measurement as a regression detector against another
build on the same machine, not as an absolute guarantee.

## Related documents

* [Architecture](Architecture.md) — how the modules fit together.
* [Building](Building.md) — producing the `RelWithDebInfo` build these measurements assume.
