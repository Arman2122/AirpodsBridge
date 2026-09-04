# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: MIT

add_executable(
    AirPodsDomainTests

    AirPodsDomainTests.cpp
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
