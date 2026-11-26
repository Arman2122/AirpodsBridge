# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

# The artwork catalogue is compiled in: the view model resolves a device to its picture.
add_executable(
    AirPodsDomainTests

    AirPodsDomainTests.cpp
    "${CMAKE_SOURCE_DIR}/${APB_DEVICE_QRC_FILE}"
)

target_link_libraries(
    AirPodsDomainTests

    PRIVATE
    apb_domain
    apb_core
    apb_presentation
    Qt5::Test
)

target_include_directories(AirPodsDomainTests PRIVATE "${CMAKE_SOURCE_DIR}")

# The qrc is linked into the application target, so resource checks read the source tree instead.
target_compile_definitions(
    AirPodsDomainTests PRIVATE APB_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME AirPodsDomainTests COMMAND AirPodsDomainTests)

add_executable(AapProtocolTests AapProtocolTests.cpp)

target_link_libraries(
    AapProtocolTests

    PRIVATE
    apb_domain
    Qt5::Test
)

target_include_directories(AapProtocolTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME AapProtocolTests COMMAND AapProtocolTests)

add_executable(
    RadioServiceProtocolTests

    RadioServiceProtocolTests.cpp
    "${CMAKE_SOURCE_DIR}/Service/Protocol/ServiceProtocol.cpp"
    "${CMAKE_SOURCE_DIR}/Service/Protocol/WatchdogPolicy.cpp"
    "${CMAKE_SOURCE_DIR}/Service/Protocol/UsbIpProtocol.cpp"
    "${CMAKE_SOURCE_DIR}/Service/Protocol/HciProxyEngine.cpp"
    "${CMAKE_SOURCE_DIR}/Service/Protocol/Uac1MicEmulator.cpp"
)

target_link_libraries(
    RadioServiceProtocolTests

    PRIVATE
    Qt5::Test
)

target_include_directories(RadioServiceProtocolTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME RadioServiceProtocolTests COMMAND RadioServiceProtocolTests)

add_executable(BatteryTrackerTests BatteryTrackerTests.cpp)

target_link_libraries(
    BatteryTrackerTests

    PRIVATE
    apb_domain
    Qt5::Test
)

target_include_directories(BatteryTrackerTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME BatteryTrackerTests COMMAND BatteryTrackerTests)

add_executable(ProServicesTests ProServicesTests.cpp)

target_link_libraries(
    ProServicesTests

    PRIVATE
    apb_core
    apb_domain
    Qt5::Test
)

target_include_directories(ProServicesTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME ProServicesTests COMMAND ProServicesTests)

add_executable(QuickConnectTests QuickConnectTests.cpp)

target_link_libraries(
    QuickConnectTests

    PRIVATE
    apb_core
    Qt5::Test
    Qt5::Widgets
)

target_include_directories(QuickConnectTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME QuickConnectTests COMMAND QuickConnectTests)

add_executable(AutomationTests AutomationTests.cpp)

target_link_libraries(
    AutomationTests

    PRIVATE
    apb_domain
    apb_core
    Qt5::Test
)

target_include_directories(AutomationTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME AutomationTests COMMAND AutomationTests)

add_executable(DeviceArtworkTests DeviceArtworkTests.cpp "${CMAKE_SOURCE_DIR}/${APB_DEVICE_QRC_FILE}")

target_link_libraries(
    DeviceArtworkTests

    PRIVATE
    apb_domain
    apb_presentation
    Qt5::Gui
    Qt5::Test
)

target_include_directories(DeviceArtworkTests PRIVATE "${CMAKE_SOURCE_DIR}")

# The artwork is checked in the source tree, where the tool writes it.
target_compile_definitions(DeviceArtworkTests PRIVATE APB_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME DeviceArtworkTests COMMAND DeviceArtworkTests)

# The two palettes: every token set, text readable on every surface it lands on, in both.
add_executable(ThemeTests ThemeTests.cpp)

target_link_libraries(
    ThemeTests

    PRIVATE
    apb_presentation
    Qt5::Gui
    Qt5::Test
)

target_include_directories(ThemeTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME ThemeTests COMMAND ThemeTests)

# The frame clock every animation runs on: Qt's animations finish under it, it ticks while
# held and rests when let go. Whether the ticks land on the display's blanks is not a thing
# a test runner can promise; the clock's log line on a real machine is.
add_executable(FrameClockTests FrameClockTests.cpp)

target_link_libraries(
    FrameClockTests

    PRIVATE
    apb_gui
    Qt5::Widgets
    Qt5::Test
)

target_include_directories(FrameClockTests PRIVATE "${CMAKE_SOURCE_DIR}")

add_test(NAME FrameClockTests COMMAND FrameClockTests)

# Not a test: renders every page of the settings window to PNG files, in light and in dark,
# so the window can be checked by eye -- and screenshotted for the README -- without a tray
# icon, a Bluetooth adapter or the registry. Usage: SettingsPreview <output directory>
add_executable(SettingsPreview SettingsPreview.cpp "${CMAKE_SOURCE_DIR}/${APB_QRC_FILE}")

target_link_libraries(
    SettingsPreview

    PRIVATE
    apb_gui
    apb_core
    apb_presentation
    Qt5::Widgets
)

target_include_directories(SettingsPreview PRIVATE "${CMAKE_SOURCE_DIR}")

# Not a test either: renders the battery popup for every model to PNG files, which is the
# only way to look at the artwork without a device. Usage: PopupPreview <output directory>
add_executable(
    PopupPreview
    PopupPreview.cpp
    "${CMAKE_SOURCE_DIR}/${APB_QRC_FILE}"
    "${CMAKE_SOURCE_DIR}/${APB_DEVICE_QRC_FILE}"
)

target_link_libraries(
    PopupPreview

    PRIVATE
    apb_gui
    apb_core
    apb_presentation
    Qt5::Widgets
)

target_include_directories(PopupPreview PRIVATE "${CMAKE_SOURCE_DIR}")
