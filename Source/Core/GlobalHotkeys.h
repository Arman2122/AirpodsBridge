// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <QObject>
#include <QString>

namespace Core::GlobalHotkeys {

enum class Action : uint32_t {
    ToggleConnect = 1,
    TogglePopup = 2,
    CycleNoiseControl = 3,
};

struct HotkeySpec {
    bool ctrl{false};
    bool alt{false};
    bool shift{false};
    bool win{false};
    // Win32 virtual key code (e.g. 'A'..'Z', '0'..'9', VK_F1..VK_F12).
    uint32_t virtualKey{0};

    bool operator==(const HotkeySpec &) const = default;
};

// Pure string <-> HotkeySpec codec (e.g. "Ctrl+Alt+A"). Returns `std::nullopt` for empty or
// invalid shortcuts, or shortcuts without at least one modifier key (except function keys).
std::optional<HotkeySpec> ParseHotkey(const QString &spec);
QString FormatHotkey(const HotkeySpec &spec);

//
// Native Win32 `RegisterHotKey` manager on a dedicated hidden `QWindow`.
// Zero low-level keyboard hooks, zero polling, zero idle CPU.
//
class Manager : public QObject
{
    Q_OBJECT

public:
    explicit Manager(QObject *parent = nullptr);
    ~Manager() override;

    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;

    // Empty `spec` unregisters the action and returns true. Non-empty returns true only
    // when parsed and accepted by Windows (returns false if another app owns the hotkey).
    bool SetBinding(Action action, const QString &spec);
    void ClearAll();

signals:
    void Triggered(Core::GlobalHotkeys::Action action);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Core::GlobalHotkeys
