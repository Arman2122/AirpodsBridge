# Building

AirpodsBridge builds as a native **64-bit Windows (`x64` and `ARM64`)** application with MSVC 2022
and **Qt 6.8.3 LTS** (including the `qtimageformats` module for `.webp` device artwork).

## Prerequisites

| Component | Version | Notes |
| --- | --- | --- |
| CMake | 3.20 or newer | [Download](https://cmake.org/download/) |
| Visual Studio | 2022 | **Desktop development with C++** (`x64` toolset; add `C++ ARM64 build tools` for ARM64 cross-compilation) |
| Qt | 6.8.3 LTS | `win64_msvc2022_64` for x64; add `win64_msvc2022_arm64_cross_compiled` for ARM64, both with `qtimageformats` |
| vcpkg | any recent checkout | Supplies cpr, cxxopts, nlohmann-json, magic-enum and the Boost headers |
| Python | 3.9 or newer | Only for the tools in `Tools/` |
| NSIS | latest | Only for `APB_GENERATE_INSTALLER=ON` |

### Visual Studio Build Tools

The full IDE is not required:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended"
```

To also cross-compile native Windows ARM64 binaries from an x64 host, add `--add Microsoft.VisualStudio.Component.VC.Tools.ARM64`.

### Qt

[`aqtinstall`](https://github.com/miurahr/aqtinstall) installs Qt 6.8.3 LTS cleanly without a Qt
account. Include `-m qtimageformats` so `qwebp.dll` is present for Apple `.webp` device artwork:

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt -m qtimageformats
```

That produces `C:\Qt\6.8.3\msvc2022_64`. For ARM64 cross-compilation on the same machine, install
the ARM64 target alongside it under the same `C:\Qt` root so `qtpaths6.bat` finds the host tools:

```powershell
aqt install-qt windows desktop 6.8.3 win64_msvc2022_arm64_cross_compiled -O C:\Qt -m qtimageformats
```

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
$env:QT_ROOT_DIR = "C:\Qt\6.8.3\msvc2022_64"
$env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH"

cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Set `VCPKG_ROOT` and `QT_ROOT_DIR` permanently with `setx` so new shells inherit them.

| Preset | Output | Purpose |
| --- | --- | --- |
| `dev` | `Build/dev` | Native x64 build with tests on and console attached so log output is visible |
| `release` | `Build/release` | Native x64 release build with tests on and NSIS installer generated |
| `release-arm64` | `Build/release-arm64` | Native ARM64 release build cross-compiled with `QT_HOST_PATH` and NSIS installer |

## Build without presets

The presets are a convenience, not a requirement:

```powershell
# Native x64
cmake -S . -B Build/x64 -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 `
  -DAPB_BUILD_TESTS=ON

cmake --build Build/x64 --config RelWithDebInfo

# Native ARM64 cross-compiled from x64 host
cmake -S . -B Build/arm64 -G "Visual Studio 17 2022" -A ARM64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_arm64 `
  -DQT_HOST_PATH=C:/Qt/6.8.3/msvc2022_64 `
  -DVCPKG_TARGET_TRIPLET=arm64-windows `
  -DVCPKG_HOST_TRIPLET=x64-windows

cmake --build Build/arm64 --config RelWithDebInfo
```

The executable and the Qt 6 runtime deployed beside it land in `Build/<arch>/Binary/`.

To build the companion No-Test-Mode Radio Proxy Service (`AirpodsBridgeService.exe`, zero external
runtimes) for either architecture:

```powershell
cmake -S Service -B Build/service-x64 -G "Visual Studio 17 2022" -A x64
cmake --build Build/service-x64 --config RelWithDebInfo

# Or for Windows ARM64:
cmake -S Service -B Build/service-arm64 -G "Visual Studio 17 2022" -A ARM64
cmake --build Build/service-arm64 --config RelWithDebInfo
```

The first configure builds every vcpkg dependency from source and takes considerably longer
than the project itself. Later configures reuse the binary cache.

## Build options

| Option | Default | Effect |
| --- | --- | --- |
| `APB_BUILD_TESTS` | `OFF` | Build the Qt Test suites in `Tests/` |
| `APB_ENABLE_CONSOLE` | `OFF` | Build as a console application so log output is visible |
| `APB_GENERATE_INSTALLER` | `OFF` | Run CPack/NSIS after the build |
| `APB_QT_DEPLOY` | `ON` | Run `windeployqt` after the build (uses `QT_HOST_PATH` + `qtpaths6.bat` when cross-compiling) |
| `APB_GITHUB_OWNER` | `Arman2122` | Repository owner the updater queries |
| `APB_GITHUB_REPOSITORY` | `AirpodsBridge` | Repository name the updater queries |
| `APB_RELEASE_TAG_PREFIX` | `v` | Release tag prefix of that repository |
| `APB_BUILD_GIT_HASH` | unset | Commit hash shown in the About pane |

## Tests

```powershell
ctest --preset dev
```

`ctest` needs the Qt 6 DLLs on `PATH`. `windeployqt` places them beside the application, but
the test executables resolve them from the environment:

```powershell
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
```

## Formatting

`.clang-format` is authoritative. Format the files you touched:

```powershell
clang-format -i Source\Domain\AppleCP.cpp
```

CI runs the same check over `Source/`, `Service/`, and `Tests/` with `--dry-run --Werror`, using
clang-format 19. Version 18 or newer is required, because earlier releases ignore
`.clang-format-ignore`. Visual Studio ships one at
`VC\Tools\Llvm\x64\bin\clang-format.exe`, or install it with `pip install clang-format==19.1.5`.

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

**`Could NOT find Qt6`** — `CMAKE_PREFIX_PATH` points at the wrong directory. It must be
the directory *containing* `bin/` and `lib/cmake/`, for example `C:/Qt/6.8.3/msvc2022_64`,
not `C:/Qt` and not the `bin` directory.

**`DeviceArtworkTests` fails on `.webp` assets** — Qt was installed without the `qtimageformats`
module. Re-run `aqt install-qt ... -m qtimageformats` so `plugins/imageformats/qwebp.dll` is
available.

**`failed to execute: git rev-parse` from vcpkg** — the vcpkg clone is shallow. Run
`git -C C:\vcpkg fetch --unshallow`.

**Linker errors mentioning Win32/x86 or wrong machine type** — the generator is building for the
wrong architecture. `-A x64` or `-A ARM64` must be on the *configure* line; adding it to the
build line has no effect.

**CMake 4.x rejects a dependency's `cmake_minimum_required`** — some `FetchContent`
dependencies predate CMake 4. Add `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` when configuring.

**A preset fails with an empty toolchain path** — `VCPKG_ROOT` or `QT_ROOT_DIR` is not set
in the current shell.
