# Building

AirpodsBridge builds as a **32-bit (Win32)** Windows application with MSVC and Qt 5.15.2.
The 32-bit target is not incidental: Qt 5.15.2's `msvc2019` binaries and the project's
Windows Runtime usage are what the build is verified against.

## Prerequisites

| Component | Version | Notes |
| --- | --- | --- |
| CMake | 3.20 or newer | [Download](https://cmake.org/download/) |
| Visual Studio | 2019 or 2022 | **Desktop development with C++**, including the x86 toolset |
| Qt | 5.15.2, `win32_msvc2019` | The 32-bit build, not `msvc2019_64` |
| vcpkg | any recent checkout | Supplies cpr, cxxopts, nlohmann-json, magic-enum and the Boost headers |
| Python | 3.9 or newer | Only for the tools in `Tools/` |
| NSIS | latest | Only for `APB_GENERATE_INSTALLER=ON` |

### Visual Studio Build Tools

The full IDE is not required:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended"
```

### Qt

The official online installer keeps 5.15.2 behind its "Archive" filter and requires an
account. [`aqtinstall`](https://github.com/miurahr/aqtinstall) needs neither:

```powershell
pip install aqtinstall
aqt install-qt windows desktop 5.15.2 win32_msvc2019 --outputdir C:\Qt
```

That produces `C:\Qt\5.15.2\msvc2019`.

### vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

Clone without `--depth`. `vcpkg.json` pins a `builtin-baseline` commit, and a shallow clone
cannot resolve it.

## Build with presets

Point two environment variables at the tools, then use the presets in
[`CMakePresets.json`](/CMakePresets.json):

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
$env:QT_ROOT_DIR = "C:\Qt\5.15.2\msvc2019"

cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Set both permanently with `setx` so new shells inherit them.

| Preset | Output | Purpose |
| --- | --- | --- |
| `dev` | `Build/dev` | Tests on, console attached so log output is visible |
| `release` | `Build/release` | Tests on, NSIS installer generated |
| `vs2019` | `Build/vs2019` | Same as `dev`, for Visual Studio 2019 |

## Build without presets

The presets are a convenience, not a requirement:

```powershell
cmake -S . -B Build -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019 `
  -DAPB_BUILD_TESTS=ON

cmake --build Build --config RelWithDebInfo
```

The executable and the Qt runtime deployed beside it land in `Build/Binary/`.

The first configure builds every vcpkg dependency from source and takes considerably longer
than the project itself. Later configures reuse the binary cache.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `APB_BUILD_TESTS` | `OFF` | Build the Qt Test suites in `Tests/` |
| `APB_ENABLE_CONSOLE` | `OFF` | Build as a console application so log output is visible |
| `APB_GENERATE_INSTALLER` | `OFF` | Run CPack/NSIS after the build |
| `APB_QT_DEPLOY` | `ON` | Run `windeployqt` after the build |
| `APB_GITHUB_OWNER` | `Arman2122` | Repository owner the updater queries |
| `APB_GITHUB_REPOSITORY` | `AirpodsBridge` | Repository name the updater queries |
| `APB_RELEASE_TAG_PREFIX` | `v` | Release tag prefix of that repository |
| `APB_BUILD_GIT_HASH` | unset | Commit hash shown in the About pane |

## Tests

```powershell
ctest --preset dev
```

`ctest` needs the Qt DLLs on `PATH`. `windeployqt` places them beside the application, but
the test executables resolve them from the environment:

```powershell
$env:PATH = "C:\Qt\5.15.2\msvc2019\bin;$env:PATH"
```

## Formatting

`.clang-format` is authoritative. Format the files you touched:

```powershell
clang-format -i Source\Domain\AppleCP.cpp
```

CI runs the same check over `Source/` and `Tests/` with `--dry-run --Werror`, using
clang-format 19. Version 18 or newer is required, because earlier releases ignore
`.clang-format-ignore`. Visual Studio ships one at
`VC\Tools\Llvm\bin\clang-format.exe`, or install it with `pip install clang-format==19.1.5`.

## Regenerating the icon

`Icon.svg` and `Icon.ico` are both generated, so edit
[`Tools/generate_icon.py`](/Tools/generate_icon.py) rather than the assets:

```powershell
pip install pillow svglib rlPyCairo
python Tools\generate_icon.py
```

The script writes the SVG first and rasterises the ICO from it, so the two stay in sync.
`svglib` renders through `renderPM`, whose backend is `rlPyCairo`. If `cairocffi` is also
installed it takes precedence over `pycairo` and then fails to find `libcairo-2.dll`, so
uninstall `cairocffi` (and `cairosvg`, which pulls it in).

## Troubleshooting

**`Could NOT find Qt5`** — `CMAKE_PREFIX_PATH` points at the wrong directory. It must be
the directory *containing* `bin/` and `lib/cmake/`, for example `C:/Qt/5.15.2/msvc2019`,
not `C:/Qt` and not the `bin` directory.

**`failed to execute: git rev-parse` from vcpkg** — the vcpkg clone is shallow. Run
`git -C C:\vcpkg fetch --unshallow`.

**Linker errors mentioning x64** — the generator is building for the wrong architecture.
`-A Win32` must be on the *configure* line; adding it to the build line has no effect.

**CMake 4.x rejects a dependency's `cmake_minimum_required`** — some `FetchContent`
dependencies predate CMake 4. Add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` when configuring.

**A preset fails with an empty toolchain path** — `VCPKG_ROOT` or `QT_ROOT_DIR` is not set
in the current shell.
