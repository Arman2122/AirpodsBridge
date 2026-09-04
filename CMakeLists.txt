# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20)

set(APB_GITHUB_OWNER "Arman2122" CACHE STRING "GitHub repository owner used by the updater.")
set(
    APB_GITHUB_REPOSITORY
    "AirpodsBridge"
    CACHE STRING
    "GitHub repository name used by the updater."
)
set(
    APB_RELEASE_TAG_PREFIX
    "v"
    CACHE STRING
    "Prefix the release tags of the configured repository use (\"v\" for v1.2.3, empty for 1.2.3)."
)

project(
    AirpodsBridge
    VERSION 1.0.0 # Keep in sync with `version-string` in `vcpkg.json`
    LANGUAGES C CXX
    DESCRIPTION "AirPods battery, ear detection, and quick connect for Windows"
    HOMEPAGE_URL "https://github.com/${APB_GITHUB_OWNER}/${APB_GITHUB_REPOSITORY}"
)

include(FetchContent)
include(CMake/Utils.cmake)

if(MSVC AND ${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.20.4")
    #
    # Workaround
    #
    # The current C++20 <format> is defective, and some ABI-breaking changes will be applied soon.
    # So we can't use it in the MSVC stable ABI C++20 (compiler option `/std:c++20`).
    # The way to use <format> now is to specify the `/std:c++latest` option to MSVC.
    #
    # But since CMake 3.20.4, when the value of `CMAKE_CXX_STANDARD` is `20`, the specified option
    # is no longer `/std:c++latest` but `/std:c++20`.
    #
    # There are two workarounds:
    # 1. Set `CMAKE_CXX_STANDARD` to `23`, which makes CMake specify `/std:c++latest` to the compiler.
    # 2. Modify the compiler option specified when `CMAKE_CXX_STANDARD` is `20`.
    #
    # Here we choose the second one, because I think it is relatively cleaner.
    #
    # TODO: Remove this workaround when <format> is available in `/std:c++20`.
    #
    # For more details:
    # https://github.com/microsoft/STL/issues/1814
    # https://gitlab.kitware.com/cmake/cmake/-/commit/3aaf1d91bf353559aef7ec56ad35924383ed613a
    #
    set(CMAKE_CXX20_STANDARD_COMPILE_OPTION "-std:c++latest")
    set(CMAKE_CXX20_EXTENSION_COMPILE_OPTION "-std:c++latest")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(VCPKG_FEATURE_FLAGS "versions")

##################################################
# Build options
#

set(APB_BUILD_TESTS OFF CACHE BOOL "Build first-party tests.")
set(APB_ENABLE_CONSOLE OFF CACHE BOOL "Enable console.")
set(APB_GENERATE_INSTALLER OFF CACHE BOOL "Generate installer after build.")
set(APB_QT_DEPLOY ON CACHE BOOL "Run Qt deployment tool after build")

##################################################

set(APB_BINARY_OUT_DIR "${CMAKE_BINARY_DIR}/Binary")
set(
    APB_OUTPUT_DIRECTORIES

    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO

    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL
    CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO

    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO
)
foreach (OUTPUT_DIRECTORY ${APB_OUTPUT_DIRECTORIES})
    set(${OUTPUT_DIRECTORY} ${APB_BINARY_OUT_DIR})
endforeach()

##################################################
# Global options
#

if (MSVC)
    
    add_compile_options(
        "/MP"           # Multi-processor compilation

        "/await"        # Enable the legacy pre-C++20 coroutine.
                        # Workaround for cppwinrt v2.0.200609.3 in C++20.
                        # For more information, see https://github.com/microsoft/cppwinrt/issues/866
    )
endif()

##################################################
# Third party
#

# Qt
#
set(APB_QT_COMPONENTS Core Gui Widgets Svg Multimedia MultimediaWidgets)
find_package(Qt5 COMPONENTS ${APB_QT_COMPONENTS} REQUIRED)
if (APB_BUILD_TESTS)
    find_package(Qt5 COMPONENTS Test REQUIRED)
endif()

# spdlog
#
add_compile_definitions(SPDLOG_WCHAR_TO_UTF8_SUPPORT SPDLOG_WCHAR_FILENAMES)
# 
# TODO: Commented out since I don't how to enable `SPDLOG_WCHAR_FILENAMES` for vcpkg, I have tried
#       `set(SPDLOG_WCHAR_FILENAMES ON)` and `add_compile_definitions(SPDLOG_WCHAR_FILENAMES)`,
#       but still not working
#
# find_package(spdlog CONFIG)
if (spdlog_FOUND)
    message("Found 'spdlog' (${spdlog_VERSION}).")
else()
    message("Fetching 'spdlog'...")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
        GIT_TAG "100f30043f33277122e0991c83845a2617172ffd" # v1.8.5
    )
    FetchContent_MakeAvailable(spdlog)
    message("Fetch 'spdlog' done.")
endif()

# cxxopts
#
find_package(cxxopts CONFIG)
if (cxxopts_FOUND)
    message("Found 'cxxopts' (${cxxopts_VERSION}).")
else()
    message("Fetching 'cxxopts'...")
    FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY "https://github.com/jarro2783/cxxopts.git"
        GIT_TAG "302302b30839505703d37fb82f536c53cf9172fa" # v2.2.1
    )
    FetchContent_MakeAvailable(cxxopts)
    message("Fetch 'cxxopts' done.")

    add_library(cxxopts::cxxopts ALIAS cxxopts)
endif()

# cpr
#
find_package(cpr 1.7.2 CONFIG)
if (cpr_FOUND)
    message("Found 'cpr' (${cpr_VERSION}).")
else()
    set(CPR_BUILD_TESTS OFF)
    set(CPR_BUILD_TESTS_SSL OFF)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    message("Fetching 'cpr'...")
    FetchContent_Declare(
        cpr
        GIT_REPOSITORY "https://github.com/whoshuu/cpr.git"
        GIT_TAG "beb9e98806bb84bcc130a2cebfbcbbc6ce62b335" # v1.7.2
    )
    FetchContent_MakeAvailable(cpr)
    message("Fetch 'cpr' done.")
endif()

# json
#
find_package(nlohmann_json CONFIG)
if (nlohmann_json_FOUND)
    message("Found 'nlohmann_json' (${nlohmann_json_VERSION}).")
else()
    message("Fetching 'nlohmann_json'...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY "https://github.com/nlohmann/json.git"
        GIT_TAG "db78ac1d7716f56fc9f1b030b715f872f93964e4" # v3.9.1
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message("Fetch 'nlohmann_json' done.")
endif()

# SingleApplication
#
set(QAPPLICATION_CLASS "QApplication" CACHE STRING "Inheritance class for SingleApplication")
message("Fetching 'SingleApplication'...")
FetchContent_Declare(
    SingleApplication
    GIT_REPOSITORY "https://github.com/itay-grudev/SingleApplication.git"
    GIT_TAG "bdbb09b5f21ebea4cd7dfb43b29114a94e04a3a1" # v3.3.0
)
FetchContent_MakeAvailable(SingleApplication)
message("Fetch 'SingleApplication' done.")

# magic_enum
#
find_package(magic_enum CONFIG)
if (magic_enum_FOUND)
    message("Found 'magic_enum' (${magic_enum_VERSION}).")
else()
    message("Fetching 'magic_enum'...")
    FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY "https://github.com/Neargye/magic_enum.git"
        GIT_TAG "3d1f6a5a2a3fbcba077e00ad0ccc2dd9fefc2ca7" # v0.7.3
    )
    FetchContent_MakeAvailable(magic_enum)
    message("Fetch 'magic_enum' done.")
endif()

#
# Boost libraries
#

# stacktrace
#
if (MSVC)
    set(APB_STACKTRACE_COMPONENT stacktrace_windbg)
endif()

# Find boost-stacktrace using pkg-config or manually
find_path(BOOST_STACKTRACE_INCLUDE_DIR boost/stacktrace.hpp)
include_directories(${BOOST_STACKTRACE_INCLUDE_DIR})

# Find boost-pfr
find_path(BOOST_PFR_INCLUDE_DIR boost/pfr.hpp)
include_directories(${BOOST_PFR_INCLUDE_DIR})

# Create interface targets for boost libraries
if(NOT TARGET Boost::stacktrace_windbg)
    add_library(Boost::stacktrace_windbg INTERFACE IMPORTED)
    set_target_properties(Boost::stacktrace_windbg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BOOST_STACKTRACE_INCLUDE_DIR}")
endif()

if(NOT TARGET Boost::pfr)
    add_library(Boost::pfr INTERFACE IMPORTED)
    set_target_properties(Boost::pfr PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BOOST_PFR_INCLUDE_DIR}")
endif()

##################################################

set(
    APB_SUPPORT_CODE_FILES

    "Source/Support/Opts.cpp"
    "Source/Support/Logger.cpp"
    "Source/Support/Assert.cpp"
    "Source/Support/Error.cpp"
)

set(
    APB_DOMAIN_CODE_FILES

    "Source/Domain/AirPodsState.cpp"
    "Source/Domain/AirPodsAdvertisement.cpp"
    "Source/Domain/AirPodsStateManager.cpp"
    "Source/Domain/AppleCP.cpp"
)

set(
    APB_CORE_CODE_FILES

    "Source/Core/Debug.cpp"
    "Source/Core/Update.cpp"
    "Source/Core/AirPods.cpp"
    "Source/Core/QuickConnect.cpp"
    "Source/Core/Settings.cpp"
    "Source/Core/SettingsRepository.cpp"
)

set(
    APB_PRESENTATION_CODE_FILES

    "Source/Presentation/MainWindowPresentation.cpp"
    "Source/Presentation/Theme.cpp"
)

set(
    APB_GUI_CODE_FILES

    "Source/Gui/TrayIcon.cpp"
    "Source/Gui/TaskbarStatus.cpp"
    "Source/Gui/MainWindow.cpp"
    "Source/Gui/SelectWindow.cpp"
    "Source/Gui/DownloadWindow.cpp"
    "Source/Gui/SettingsWindow.cpp"
    "Source/Gui/Widget/Battery.cpp"
    "Source/Gui/Widget/PushButton.cpp"
)

set(APB_APP_CODE_FILES
    "Source/App/Main.cpp"
    "Source/App/Application.cpp"
)

set(ADD_EXECUTABLE_ARG)

if (WIN32)
    list(APPEND APB_CORE_CODE_FILES
        "Source/Core/AutoStart_win.cpp"
        "Source/Core/Bluetooth_win.cpp"
        "Source/Core/GlobalMedia_win.cpp"
        "Source/Core/QuickConnect_win.cpp"
        "Source/Core/WindowEffects_win.cpp"
    )
    list(APPEND APB_APP_CODE_FILES
        "Source/Resource/Application.manifest"
        "Source/Resource/Resource.rc"
    )
    if (NOT APB_ENABLE_CONSOLE)
        set(ADD_EXECUTABLE_ARG WIN32)
    endif()

    # Workaround for: "_MSC_VER" is undefined in .rc files
    #
    set_source_files_properties("Source/Resource/Resource.rc" PROPERTIES COMPILE_FLAGS "/d_MSC_VER")
endif()

##################################################
# Qt configurations
#

# Resource

set(APB_QRC_FILE "Source/Resource/Resource.qrc")

##################################################

add_library(apb_support STATIC ${APB_SUPPORT_CODE_FILES})

add_library(apb_domain STATIC ${APB_DOMAIN_CODE_FILES})

add_library(apb_core STATIC ${APB_CORE_CODE_FILES})

add_library(apb_presentation STATIC ${APB_PRESENTATION_CODE_FILES})

add_library(apb_gui STATIC ${APB_GUI_CODE_FILES})

set(
    APB_INTERNAL_TARGETS
    apb_support
    apb_domain
    apb_core
    apb_presentation
    apb_gui
)

set_target_properties(${APB_INTERNAL_TARGETS} SingleApplication PROPERTIES DEBUG_POSTFIX "d")

#
# Internal headers are included root-anchored, as "Source/<Module>/<File>.h". That makes the
# module a file crosses into visible at every call site, and keeps same-named headers in
# different modules (Support/Utils.h vs Gui/Utils.h) unambiguous.
#
foreach (APB_TARGET ${APB_INTERNAL_TARGETS})
    target_include_directories(
        ${APB_TARGET} PUBLIC "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/Source"
    )
endforeach()

target_include_directories(
    apb_gui PUBLIC

    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/Source"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/apb_gui_autogen/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/apb_gui_autogen/include_$<CONFIG>>"
)

target_link_libraries(
    apb_support

    PUBLIC
    Qt5::Core
    Qt5::Gui
    spdlog::spdlog
    cxxopts::cxxopts
    magic_enum::magic_enum
    Boost::${APB_STACKTRACE_COMPONENT}
)

target_link_libraries(
    apb_domain

    PUBLIC
    apb_support
)

target_link_libraries(
    apb_core

    PUBLIC
    apb_domain
    cpr::cpr
    nlohmann_json::nlohmann_json
    magic_enum::magic_enum
    Boost::pfr
)

if (WIN32)
    target_link_libraries(apb_core PRIVATE Ole32 Uuid Ksuser Dwmapi)
endif()

target_link_libraries(
    apb_presentation

    PUBLIC
    apb_domain
)

target_link_libraries(
    apb_gui

    PUBLIC
    apb_core
    apb_presentation
    Qt5::Widgets
    Qt5::Svg
    Qt5::Multimedia
    Qt5::MultimediaWidgets
)

add_executable(
    ${PROJECT_NAME} ${ADD_EXECUTABLE_ARG}

    ${APB_APP_CODE_FILES}
    ${APB_QRC_FILE}
)

foreach (APB_TARGET ${APB_INTERNAL_TARGETS} ${PROJECT_NAME})
    set(APB_HEADER_DEFINITION_VISIBILITY PRIVATE)
    if (APB_TARGET IN_LIST APB_INTERNAL_TARGETS)
        set(APB_HEADER_DEFINITION_VISIBILITY PUBLIC)
    endif()

    target_compile_definitions(
        ${APB_TARGET} ${APB_HEADER_DEFINITION_VISIBILITY}

        $<$<CONFIG:Debug>:APB_DEBUG>
    )
    target_compile_definitions(${APB_TARGET} PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

    if (WIN32)
        target_compile_definitions(
            ${APB_TARGET} ${APB_HEADER_DEFINITION_VISIBILITY}
            APB_OS_WIN
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
    endif()
    if (MSVC)
        target_compile_definitions(${APB_TARGET} PRIVATE APB_MSVC)
    endif()
    if (APB_ENABLE_CONSOLE)
        target_compile_definitions(
            ${APB_TARGET} ${APB_HEADER_DEFINITION_VISIBILITY} APB_ENABLE_CONSOLE
        )
    endif()
    if (APB_BUILD_GIT_HASH)
        target_compile_definitions(${APB_TARGET} PRIVATE APB_BUILD_GIT_HASH="${APB_BUILD_GIT_HASH}")
    endif()
endforeach()

target_include_directories(
    ${PROJECT_NAME} PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/Source"
)

##################################################

if (MSVC)
    if (APB_QT_DEPLOY)
        #
        # `--no-translations` keeps windeployqt from copying Qt's own .qm catalogues
        # next to the executable. This application ships English only, so those 23
        # files (~3.8 MB) would never be loaded.
        #
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND "${Qt5_DIR}/../../../bin/windeployqt"
                    --no-translations
                    "${APB_BINARY_OUT_DIR}/${PROJECT_NAME}.exe"
            COMMENT "Deploying Qt..."
        )
    endif()
endif()

##################################################
# Installer generate
#

if (APB_GENERATE_INSTALLER)
    set(CPACK_PACKAGE_VENDOR ${PROJECT_NAME})
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/Installer")
    set(CPACK_PACKAGE_VERSION_MAJOR ${AirpodsBridge_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${AirpodsBridge_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${AirpodsBridge_VERSION_PATCH})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${PROJECT_NAME})
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME}" ${PROJECT_NAME})

    set(ICON_FILEPATH "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resource/Image/Icon.ico")
    set(SHORTCUT_NAME "AirpodsBridge")

    if (MSVC)
        set(CPACK_GENERATOR NSIS)
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
        set(CPACK_NSIS_URL_INFO_ABOUT ${CMAKE_PROJECT_HOMEPAGE_URL})
        set(CPACK_NSIS_EXECUTABLES_DIRECTORY "./")
        set(CPACK_NSIS_MUI_FINISHPAGE_RUN "./${PROJECT_NAME}.exe")
        set(CPACK_NSIS_BRANDING_TEXT "${PROJECT_NAME} installer")
        string(REPLACE "/" "\\\\" ICON_FILEPATH ${ICON_FILEPATH})
        set(CPACK_NSIS_MUI_ICON ${ICON_FILEPATH})

        # Create and remove shortcut
        #
        # Workaround for: https://gitlab.kitware.com/cmake/cmake/-/issues/15982
        #                 (I can't believe this is an unsolved issue 5 years old lol..)
        #
        set(
            CPACK_NSIS_EXTRA_INSTALL_COMMANDS
            "CreateShortCut \\\"$DESKTOP\\\\${SHORTCUT_NAME}.lnk\\\" \\\"$INSTDIR\\\\${PROJECT_NAME}.exe\\\""
        )
        set(
            CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
            "Delete \\\"$DESKTOP\\\\${SHORTCUT_NAME}.lnk\\\""
        )
    endif()

    # Prevent CMake from adding "include" and "lib" directories
    #
    install(
        DIRECTORY "${APB_BINARY_OUT_DIR}/"
        DESTINATION "."
        COMPONENT ${PROJECT_NAME}
        PATTERN "*.ilk" EXCLUDE
        PATTERN "*.exp" EXCLUDE
        PATTERN "*.lib" EXCLUDE
        # Compile PDBs for the static libraries and the vendored dependencies are ~90 MB and
        # are useless to a user: the application's own linker PDB is what boost::stacktrace
        # resolves frames against, and that one is kept.
        PATTERN "apb_*.pdb" EXCLUDE
        PATTERN "spdlog*.pdb" EXCLUDE
        PATTERN "SingleApplication*.pdb" EXCLUDE
        # Release CI configures with APB_BUILD_TESTS=ON, so every test executable lands here.
        # Match them by suffix rather than by name, so a new test target cannot ship by accident.
        PATTERN "*Tests.exe" EXCLUDE
        PATTERN "*Tests.pdb" EXCLUDE
    )
    #
    # The MIT licence requires its text to travel with every copy of the software.
    # `CPACK_RESOURCE_FILE_LICENSE` only renders it on the installer's accept page, so the
    # file is installed as well to reach the install tree and the portable package.
    #
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "."
        COMPONENT ${PROJECT_NAME}
    )

    #
    # Qt is redistributed as DLLs under the LGPLv3, which requires its licence text and an
    # attribution notice to travel with the binary. LGPLv3 is written as additional
    # permissions on top of GPLv3 and incorporates it by reference, so both texts ship.
    #
    install(
        FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Qt/LICENSE.LGPLv3"
            "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Qt/LICENSE.GPLv3"
            "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Qt/README.md"
        DESTINATION "Licenses/Qt"
        COMPONENT ${PROJECT_NAME}
    )

    #
    # The Inter typeface is embedded in the executable's resources. The SIL Open Font
    # License requires its notice to accompany the font wherever it travels.
    #
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/Inter/LICENSE.txt"
        DESTINATION "Licenses/Inter"
        COMPONENT ${PROJECT_NAME}
    )

    set(CPACK_COMPONENTS_ALL ${PROJECT_NAME})

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND "${CMAKE_CPACK_COMMAND}" "-C" "$<CONFIGURATION>"
        COMMENT "Generating installer..."
    )
endif()
include(CPack) # We always need variable "CPACK_SYSTEM_NAME"

##################################################
# Configure
#

configure_file("Source/Config.h.in" "Source/Config.h")

##################################################

target_link_libraries(
    ${PROJECT_NAME}

    PRIVATE
    apb_gui
    SingleApplication::SingleApplication
)

##################################################
# Tests
#

if (APB_BUILD_TESTS)
    include(CTest)
    add_subdirectory(Tests)
endif()

##################################################

#
# Use PDB file name as alternate PDB path, it is used to make stacktrace work
# See https://github.com/boostorg/stacktrace/issues/55
#
if (MSVC)
    apply_pdbaltpath_pdb_for_all_targets()
endif()
