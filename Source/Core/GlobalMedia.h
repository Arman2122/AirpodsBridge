// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#if defined APB_OS_WIN
    #include "Source/Core/GlobalMedia_win.h"
#endif

namespace Core::GlobalMedia {

inline void Play()
{
    Controller::GetInstance().Play();
}

inline void Pause()
{
    Controller::GetInstance().Pause();
}
} // namespace Core::GlobalMedia
