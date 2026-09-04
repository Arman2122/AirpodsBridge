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
| BLE scanning follows the bound device's connection state, and unrelated manufacturer data is rejected before it is parsed | `Source/Core/AirPods.cpp`, `Source/Domain/AirPodsAdvertisement.cpp` |
| Hidden tray-popup animations stop decoding frames | `Source/Gui/MainWindow.cpp` |
| Tray icons are reused until the rendered battery state actually changes | `Source/Gui/TrayIcon.cpp` |
| Taskbar geometry polling slows down after its initial positioning pass | `Source/Gui/TaskbarStatus.cpp` |

The tray-icon cache is the easiest of these to break: `TrayIcon` compares an
`IconRenderState` and re-renders only on a difference. Adding a field that changes on every
tick — a timestamp, an animation frame — silently reintroduces continuous repainting.

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
