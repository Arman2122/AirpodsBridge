// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#if defined APB_OS_WIN
    #include "Source/Core/Bluetooth_win.h"
#endif

template <>
inline QString Helper::ToString<Core::Bluetooth::AdvertisementWatcher::ReceivedData>(
    const Core::Bluetooth::AdvertisementWatcher::ReceivedData &value)
{
    QString manufacturerData;

    for (const auto &keyValue : value.manufacturerDataMap) {
        manufacturerData +=
            QString{"CompanyId: %1 Bytes: %2"}.arg(keyValue.first).arg(ToString(keyValue.second));
    }

    return QString{"rssi: %1 address: %3\nmanufacturerData: %4"}
        .arg(value.rssi)
        .arg(value.address)
        .arg(manufacturerData);
}
