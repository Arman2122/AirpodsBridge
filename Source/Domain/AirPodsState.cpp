// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Domain/AirPodsState.h"

namespace Core::AirPods::Details {

QString ResolveDisplayName(QString deviceName, Model model)
{
    deviceName.remove(" - Find My");

    const bool isDefaultUsbCMaxName =
        model == Model::AirPods_Max_USB_C && deviceName == "AirPods Max";
    return deviceName.isEmpty() || isDefaultUsbCMaxName ? Helper::ToString(model) : deviceName;
}

} // namespace Core::AirPods::Details
