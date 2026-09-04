# Agent Guide

Operating notes for AI coding agents working in this repository. Human contributors want
[CONTRIBUTING.md](CONTRIBUTING.md); this file is the same rules stated as constraints, plus
the things that are easy to get wrong here.

## 1. What this project is

A Qt 5, C++20 tray application for Windows. It decodes the Bluetooth Low Energy
proximity-pairing advertisements AirPods broadcast, and shows battery and in-ear state in
the notification area.

Read [Docs/Architecture.md](Docs/Architecture.md) before any change that moves code between
directories or adds a dependency between modules.

## 2. Hard constraints

These are settled. Do not "fix" them, and do not reintroduce what was removed.

| Constraint | Detail |
| --- | --- |
| **Windows only** | Platform code uses the `_win.cpp` / `_win.h` suffix and `APB_OS_WIN`. No other port is maintained. |
| **32-bit only** | `-A Win32` belongs on the *configure* line. The build is verified only against Qt 5.15.2 `win32_msvc2019`. |
| **English only** | No `.ts` catalogues, no `QTranslator`, no localisation machinery. `tr()` call sites remain because Qt resolves them to the source English text — leave them alone, and do not add localisation back. |
| **`AirPods` is a device name** | Never treat it as a branding token to be renamed. |
| **Generated assets** | `Source/Resource/Image/Icon.svg` and `Icon.ico` are both output of `Tools/generate_icon.py`. Edit the generator, never the assets. |
| **`Build/` is disposable** | Never commit anything under it. |

## 3. Module layout

Each directory under `Source/` is exactly one CMake target. Dependencies point one way.

```
apb_support  ->  apb_domain  ->  apb_core          ->  apb_gui  ->  AirpodsBridge
                 apb_domain  ->  apb_presentation  ->  apb_gui
```

| Directory | Target | Rule |
| --- | --- | --- |
| `Source/Support/` | `apb_support` | No domain knowledge. |
| `Source/Domain/` | `apb_domain` | No platform calls, no widgets. This is the testable layer. |
| `Source/Core/` | `apb_core` | Windows-facing services. |
| `Source/Presentation/` | `apb_presentation` | View models. Does **not** link `apb_core`. |
| `Source/Gui/` | `apb_gui` | Qt widgets and `.ui` forms. |
| `Source/App/` | `AirpodsBridge` | Entry point and wiring. |

Adding a source file means adding it to the matching `APB_*_CODE_FILES` list in
`CMakeLists.txt`. Putting a file in a directory does not build it.

One known exception is documented in
[Architecture](Docs/Architecture.md#known-wart): `Source/Domain/AirPodsAdvertisement.h`
includes `Source/Core/Bluetooth.h` for a value type. Do not use it to justify new
domain-to-core includes.

## 4. Conventions

**Includes.** Internal headers are root-anchored, so the module is visible at the call site:

```cpp
#include "Source/Domain/AirPodsState.h"   // internal
#include <QObject>                        // third party
#include <Config.h>                       // generated
```

Never use relative paths such as `"../Support/Logger.h"`. Note that `Support/Utils.h` and
`Gui/Utils.h` both exist — root-anchoring is what keeps them apart.

**Naming.** `PascalCase` types and methods, `camelCase` locals, `_leadingUnderscore`
private members. Headers and implementations share a name. Prefixes are `APB_` (macros,
CMake variables), `apb_` (CMake targets) and `Apb` (types).

**Licence header.** Every `.cpp`, `.h`, `.py` and CMake file starts with:

```cpp
// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT
```

CI fails without it. Use `#` comments in Python and CMake files.

**Formatting.** `.clang-format` is authoritative — four spaces, 100 columns. Run
`clang-format -i` on every file touched; CI runs `--dry-run --Werror` over `Source/` and
`Tests/`. `Source/Resource/Resource.h` is exempt via `.clang-format-ignore` because the
Visual Studio resource editor rewrites it.

## 5. Build and test

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Requires `VCPKG_ROOT` and `QT_ROOT_DIR` in the environment. `ctest` needs the Qt `bin`
directory on `PATH`. Full details and troubleshooting are in
[Docs/Building.md](Docs/Building.md).

Every change must at minimum compile in Win32 `RelWithDebInfo`. Logic changes need a test
in `Tests/`, registered in `Tests/CMakeLists.txt`.

**What automation cannot check.** Bluetooth, tray, media and settings flows have no
coverage. A green `ctest` does not mean a Bluetooth change works. Say plainly which paths
were verified and which were not.

## 6. Things that are easy to get wrong here

- **Version lives in two files.** `CMakeLists.txt` (`project(... VERSION x.y.z)`) and
  `vcpkg.json` (`version-string`). CI rejects a tag when they disagree.
- **The tray icon cache.** `TrayIcon::GenerateIcon` re-renders only when `IconRenderState`
  changes. Adding a field that varies every tick silently reintroduces continuous
  repainting. See [Docs/Performance.md](Docs/Performance.md).
- **Idle CPU is a product constraint**, not a nice-to-have. Timers, polling and animation
  in the tray path deserve scrutiny.
- **`Config.h` is generated** from `Source/Config.h.in`. Edit the template.
- **Qt AUTOMOC/AUTOUIC** means `ui_*.h` and `*.moc` includes are generated. Leave those
  include lines alone.
- **A vcpkg clone must not be shallow** — `vcpkg.json` pins a `builtin-baseline` that a
  shallow clone cannot resolve.

## 7. Output expectations

- Conventional Commit prefixes: `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`, `test:`,
  `perf:`.
- Add a `CHANGELOG.md` entry under `Unreleased` for anything a user would notice.
- Do not commit or push unless asked.
- Report honestly: if a build or test was not run, say so rather than implying it passed.
