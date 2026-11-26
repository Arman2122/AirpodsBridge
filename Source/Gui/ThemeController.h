// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <optional>

#include <QObject>

#include "Source/Core/Settings.h"
#include "Source/Core/SystemTheme.h"
#include "Source/Presentation/Theme.h"

namespace Gui {

//
// Keeps the application's look in step with the appearance setting and with Windows.
//
// The setting says light, dark, or follow Windows; Windows' own mode comes from
// `Core::SystemTheme`. The two resolve to a `Theme::Mode`, and when that changes the
// controller does everything a change needs, in this order: swaps the palette the painted
// controls read, resets the application palette the stock widgets draw from, asks the
// compositor for matching title bars on every window, and sends `Theme::ChangeEvent()` to
// every widget so the few that baked a colour into a stylesheet or a QPalette role redo
// it. Windows created later get their title bar from the event filter as they appear.
//
// Construct it before any widget. It also fixes the widget style to Fusion, once, because
// that is the one style in Qt 5 that draws entirely from its palette: the Windows style
// paints buttons and menus through the system theme, and would leave a light button in
// the middle of a dark message box.
//
class ThemeController : public QObject
{
    Q_OBJECT

public:
    explicit ThemeController(QObject *parent = nullptr);
    ~ThemeController() override;

    // The setting. Applied at once, on the calling thread, which must be the GUI thread.
    void SetAppearance(Core::Settings::Appearance appearance);

    Theme::Mode Mode() const;
    bool IsTaskbarDark() const;

    // What `appearance` means when Windows is, or is not, dark.
    static Theme::Mode Resolve(Core::Settings::Appearance appearance, bool systemDark);

Q_SIGNALS:
    void ModeChanged(Theme::Mode mode);
    void TaskbarModeChanged(bool dark);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Core::Settings::Appearance _appearance{Core::Settings::Appearance::System};
    bool _systemDark{false};
    std::optional<Theme::Mode> _applied;
    // Declared last: its callback touches the members above.
    Core::SystemTheme::Watcher _system;

    void Apply();
    void ApplyTitleBars();
};

} // namespace Gui
