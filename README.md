<div align="center">

<img src="Source/Resource/Image/Icon.svg" alt="" width="112">

# AirpodsBridge

**AirPods battery, ear detection, and quick connect — on Windows.**

[![Build](https://github.com/Arman2122/AirpodsBridge/actions/workflows/windows.yml/badge.svg)](https://github.com/Arman2122/AirpodsBridge/actions/workflows/windows.yml)
[![Lint](https://github.com/Arman2122/AirpodsBridge/actions/workflows/lint.yml/badge.svg)](https://github.com/Arman2122/AirpodsBridge/actions/workflows/lint.yml)
[![Release](https://img.shields.io/github/v/release/Arman2122/AirpodsBridge?include_prereleases&sort=semver)](https://github.com/Arman2122/AirpodsBridge/releases)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)

</div>

---

Windows pairs with AirPods and then tells you almost nothing about them: no battery
readout, no ear detection, no quick reconnect. AirpodsBridge fills that in.

It works by listening for the Bluetooth Low Energy advertisements your AirPods broadcast
continuously — the same ones an iPhone reads to draw its battery popup. Nothing is
installed on the AirPods, no Apple account is involved, and the normal Windows pairing
flow is left alone.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Install](#install)
- [Usage](#usage)
- [Supported devices](#supported-devices)
- [Building from source](#building-from-source)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [Language](#language)
- [Third-party components](#third-party-components)
- [Credits](#credits)
- [License](#license)

## Features

| | |
| --- | --- |
| **Battery levels** | Each bud and the case, in a tray popup and optionally on the tray icon or the taskbar. |
| **Automatic ear detection** | Media pauses when you take a bud out and resumes when you put it back. |
| **Quick connect** | Optionally, one click on the tray icon connects your AirPods instead of opening the window. |
| **Per-model animations** | The popup shows the device that is actually connected. |
| **Stays out of the way** | Idle CPU usage is treated as a product constraint — see [Performance](Docs/Performance.md). |

## Requirements

- Windows 10 version 1809 or newer, or Windows 11.
- A Bluetooth adapter that supports Bluetooth Low Energy.
- AirPods already paired through the normal Windows Bluetooth settings.

AirpodsBridge reads advertisements; it does not replace the Windows pairing flow.

## Install

Download the latest build from the [Releases page](https://github.com/Arman2122/AirpodsBridge/releases).

| Asset | Use it when |
| --- | --- |
| `AirpodsBridge-<version>-win32.exe` | You want a normal install, a Start menu entry and a desktop shortcut. |
| `AirpodsBridge-<version>-win32-portable.zip` | You want to unzip and run, with no installer. |
| `SHA256SUMS.txt` | You want to verify what you downloaded. |

To verify a download, compare its digest against the line for that file in `SHA256SUMS.txt`:

```powershell
Get-FileHash .\AirpodsBridge-1.0.0-win32.exe -Algorithm SHA256
```

> [!NOTE]
> Releases are unsigned unless code-signing secrets are configured for the repository, so
> Windows SmartScreen may warn the first time you run a new version. Verifying the SHA-256
> digest above is the meaningful check. See [Releasing](Docs/Releasing.md#code-signing).

## Usage

1. Pair your AirPods in **Settings → Bluetooth & devices** as usual.
2. Start AirpodsBridge. It lives in the notification area.
3. Bind your device from the main window. AirPods use randomised Bluetooth addresses, so
   the app binds to a device you choose rather than guessing; if more than one pair is
   paired, it asks which.
4. Click the tray icon for battery levels; right-click for the menu.

Ear detection, quick connect, and where battery is displayed are all toggles in the
Settings window.

**Command line**

| Option | Effect |
| --- | --- |
| `--help` | Print the available options. |
| `--trace` | Enable trace-level logging. Use this when reporting a Bluetooth bug. |

**Files it writes**

Logs are written to `%LOCALAPPDATA%\AirpodsBridge`. `AirpodsBridge.log` is the one to attach
to a bug report — launch with `--trace` first, then reproduce the problem.

Settings are not a file: `QSettings` is used at user scope in native format, so they live in
the registry under `HKCU\Software\AirpodsBridge\AirpodsBridge`.

## Supported devices

Battery and ear detection work for every model below. The animation column notes where a
device reuses another model's artwork.

| Device | Animation |
| --- | --- |
| AirPods 1 | own |
| AirPods 2 | own |
| AirPods 3 | own |
| AirPods 4 | own |
| AirPods 4 (ANC) | own |
| AirPods Pro | own |
| AirPods Pro 2 | own |
| AirPods Pro 2 (USB-C) | shares AirPods Pro 2 |
| AirPods Pro 3 | own |
| AirPods Max | own |
| AirPods Max (USB-C) | shares AirPods Max |
| Beats Fit Pro | own |

Powerbeats 3, BeatsX and BeatsSolo3 have product IDs in the code but their detection is
commented out in `Source/Domain/AppleCP.cpp`, so they are **not** currently recognised.

Missing a model? [Open an issue](https://github.com/Arman2122/AirpodsBridge/issues/new/choose)
with a `--trace` log and the product ID.

## Building from source

Requires CMake 3.20+, Visual Studio 2019/2022 with the x86 toolset, Qt 5.15.2
(`win32_msvc2019`) and a vcpkg checkout. With `VCPKG_ROOT` and `QT_ROOT_DIR` set:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Full instructions, build options and troubleshooting are in [Building](Docs/Building.md).

## Documentation

| Document | Contents |
| --- | --- |
| [Architecture](Docs/Architecture.md) | Module layout, dependency rules, how a reading reaches the tray. |
| [Building](Docs/Building.md) | Toolchain, presets, build options, troubleshooting. |
| [Releasing](Docs/Releasing.md) | Versioning, tagging, published artefacts, code signing. |
| [Performance](Docs/Performance.md) | The idle-CPU constraint and how to measure it. |
| [Contributing](CONTRIBUTING.md) | Scope, workflow, coding style, review expectations. |

## Contributing

Bug reports, new device models, performance work and Windows integration are all welcome.
Start with the [contributing guidelines](CONTRIBUTING.md).

- [Report a bug or request a feature](https://github.com/Arman2122/AirpodsBridge/issues/new/choose)
- [Open a pull request](https://github.com/Arman2122/AirpodsBridge/compare)

## Language

AirpodsBridge is **English only** and has no translation machinery. This is a deliberate
scope decision, not a missing feature — pull requests adding translations will not be
merged.

## Third-party components

| Component | License |
| --- | --- |
| [Qt 5.15.2](https://www.qt.io/download-qt-installer) | [LGPLv3](https://doc.qt.io/qt-5/lgpl.html) |
| [spdlog](https://github.com/gabime/spdlog) | [MIT](https://github.com/gabime/spdlog/blob/v1.x/LICENSE) |
| [cxxopts](https://github.com/jarro2783/cxxopts) | [MIT](https://github.com/jarro2783/cxxopts/blob/master/LICENSE) |
| [cpr](https://github.com/libcpr/cpr) | [MIT](https://github.com/libcpr/cpr/blob/master/LICENSE) |
| [nlohmann/json](https://github.com/nlohmann/json) | [MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) |
| [SingleApplication](https://github.com/itay-grudev/SingleApplication) | [MIT](https://github.com/itay-grudev/SingleApplication/blob/master/LICENSE) |
| [magic_enum](https://github.com/Neargye/magic_enum) | [MIT](https://github.com/Neargye/magic_enum/blob/master/LICENSE) |
| [Boost.PFR](https://github.com/boostorg/pfr) | [BSL-1.0](https://github.com/boostorg/pfr/blob/develop/LICENSE_1_0.txt) |
| [Boost.Stacktrace](https://github.com/boostorg/stacktrace) | [BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) |
| [Inter](https://rsms.me/inter/) | [SIL OFL 1.1](https://github.com/rsms/inter/blob/master/LICENSE.txt) |

## Credits

- [OpenPods](https://github.com/adolfintel/OpenPods) — prior art for reading AirPods
  advertisements.
- [Discontinued Privacy: Personal Data Leaks in Apple Bluetooth-Low-Energy Continuity Protocols](https://hal.inria.fr/hal-02394619/document)
  — the analysis of Apple's Continuity protocols that the parsing is based on.
- [MagicPods](https://magicpods.app/) — a commercial alternative.
- The application icon is derived from an AirPods Pro drawing from
  [the Noun Project](https://thenounproject.com/icon/3456664/).

## License

Released under the [MIT License](LICENSE).

<sub>AirPods and Beats are trademarks of Apple Inc. AirpodsBridge is not affiliated with,
authorized by, or endorsed by Apple Inc.</sub>
