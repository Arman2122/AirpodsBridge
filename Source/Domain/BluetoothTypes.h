// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>

namespace Core::Bluetooth {

//
// A single Bluetooth Low Energy advertisement handed from the platform watcher to the
// domain decoder.
//
// Plain value data with no platform handles or WinRT types, so `apb_domain` decodes and
// tests advertisements without depending on `apb_core` or Windows headers.
//
struct ReceivedAdvertisement {
    using Timestamp = std::chrono::steady_clock::time_point;

    int16_t rssi{};
    Timestamp timestamp{};
    uint64_t address{};
    std::map<uint16_t, std::vector<uint8_t>> manufacturerDataMap;
};

} // namespace Core::Bluetooth
