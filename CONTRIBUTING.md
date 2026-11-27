# Contributing

Thanks for considering a contribution. This document covers what the project accepts, how
to get a build running, and what a reviewable pull request looks like.

## Scope

AirpodsBridge deliberately keeps a narrow scope. Two decisions are settled and not open to
change:

- **English only.** There is no translation infrastructure and none will be added. See
  [Language](README.md#language) for the reasoning.
- **Windows only.** Platform code lives behind the `_win.cpp` suffix and the `APB_OS_WIN`
  macro, so a port is structurally possible, but this repository does not maintain one.

Everything else is welcome: bugs, new AirPods models, performance, Windows integration,
tests, documentation.

## Before you start

- Search the [existing issues](https://github.com/Arman2122/AirpodsBridge/issues) first.
- For anything larger than a bug fix, open an issue before writing code. It is cheaper to
  disagree about an approach in an issue than in a finished pull request.
- Bug reports involving Bluetooth are far more useful with a trace log. Launch with
  `--trace` and attach `AirpodsBridge.log` from `%LOCALAPPDATA%\AirpodsBridge`.

## Development setup

Full instructions are in [Building](Docs/Building.md). With `VCPKG_ROOT` and `QT_ROOT_DIR`
set:

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The first configure builds every vcpkg dependency and takes a while; later ones are fast.

## Architecture

Read [Architecture](Docs/Architecture.md) before moving code between directories. The short
version: each directory under `Source/` is exactly one CMake target, and dependencies point
one way only.

```
Support  ->  Domain  ->  Core          ->  Gui  ->  App
             Domain  ->  Presentation  ->  Gui
```

- `Source/Support/` — logging, assertions, errors, launch options. No domain knowledge.
- `Source/Domain/` — advertisement decoding and device state. Pure logic; this is the layer
  unit tests exercise, so keep platform calls and widgets out of it.
- `Source/Core/` — services that talk to Windows: Bluetooth, media, autostart, settings,
  updates.
- `Source/Presentation/` — view models turning domain state into something displayable. It
  does not link `apb_core`.
- `Source/Gui/` — Qt widgets and `.ui` forms.
- `Source/App/` — entry point and wiring.

If a change needs a platform call in `Domain/`, or a settings object in `Presentation/`,
the abstraction is in the wrong place. Say so in the pull request rather than adding the
link quietly.

## Coding style

- `.clang-format` is authoritative: four spaces, no tabs, 100 columns, custom brace
  wrapping. Run `clang-format -i` on every file you touch — CI checks this.
- `PascalCase` for types and methods, `camelCase` for locals, leading underscore for
  private members (`_stateMgr`).
- Header and implementation share a name (`AppleCP.h` / `AppleCP.cpp`).
- Internal includes are root-anchored: `#include "Source/Domain/AppleCP.h"`. Third-party
  and generated headers keep angle brackets.
- Project-owned identifiers use the `APB_` (macros, CMake variables), `apb_` (CMake
  targets) and `Apb` (types) prefixes. `AirPods` names the Apple device and is never a
  branding token — do not rename it.
- Every source file starts with the two-line SPDX header:

  ```cpp
  // SPDX-FileCopyrightText: 2026 Arman2122
  // SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
  ```

- Comments explain *why*, not *what*. A comment restating the code is noise; one explaining
  a Windows quirk or a non-obvious ordering constraint is valuable.

## Testing

First-party tests use Qt Test and live in `Tests/`. Name a test file after the unit under
test, such as `AirPodsStateManagerTests.cpp`, and register it in `Tests/CMakeLists.txt`.

Every change must at minimum compile in x64 `RelWithDebInfo`. Logic changes need a test.
Bluetooth, tray, media and settings flows are not covered by automation — exercise them by
hand and say in the pull request which ones you checked.

## Commits and pull requests

- Use Conventional Commit prefixes: `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`,
  `test:`, `perf:`. Imperative summary, scoped to one change.
- Keep unrelated changes in separate commits, and separate pull requests where practical.
- State the motivation, the behaviour change, and your build and test results. Link the
  issue it closes.
- Include a screenshot or recording for anything that changes the UI.
- Call out new dependencies, new settings, and any AirPods model or Windows version the
  change is specific to.
- Add a `CHANGELOG.md` entry under `Unreleased` for anything a user would notice.

## Licence

By contributing you agree that your work is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE),
matching the rest of the project.
