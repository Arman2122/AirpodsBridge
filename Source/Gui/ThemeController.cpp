// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/ThemeController.h"

#include <QApplication>
#include <QEvent>
#include <QStyleFactory>
#include <QWidget>
#include <QWindow>

#include <magic_enum.hpp>

#include "Source/Core/WindowEffects.h"
#include "Source/Support/Logger.h"

namespace Gui {

ThemeController::ThemeController(QObject *parent)
    : QObject{parent}, _system{[this](bool dark) {
          _systemDark = dark;
          Apply();
      }}
{
    _systemDark = _system.IsDark();

    // Before any widget exists, so nothing is polished twice. Fusion is compiled into
    // QtWidgets; the plugin styles are not needed.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    qApp->installEventFilter(this);

    Apply();
}

ThemeController::~ThemeController() = default;

void ThemeController::SetAppearance(Core::Settings::Appearance appearance)
{
    _appearance = appearance;
    Apply();
}

Theme::Mode ThemeController::Mode() const
{
    return _applied.value_or(Theme::Mode::Light);
}

Theme::Mode ThemeController::Resolve(Core::Settings::Appearance appearance, bool systemDark)
{
    switch (appearance) {
    case Core::Settings::Appearance::Light:
        return Theme::Mode::Light;
    case Core::Settings::Appearance::Dark:
        return Theme::Mode::Dark;
    case Core::Settings::Appearance::System:
    default:
        return systemDark ? Theme::Mode::Dark : Theme::Mode::Light;
    }
}

void ThemeController::Apply()
{
    const auto mode = Resolve(_appearance, _systemDark);
    if (_applied == mode) {
        return;
    }
    _applied = mode;

    LOG(Info, "Theme: {} (setting: {}, Windows: {}).", magic_enum::enum_name(mode),
        magic_enum::enum_name(_appearance), _systemDark ? "dark" : "light");

    Theme::SetMode(mode);
    QApplication::setPalette(Theme::ApplicationPalette(Theme::Colours()));
    ApplyTitleBars();

    // Every widget, whether or not it is on screen: a hidden settings window must come
    // back right. A widget that baked colours in redoes that on the event; the rest are
    // simply repainted.
    QEvent event{Theme::ChangeEvent()};
    const auto widgets = QApplication::allWidgets();
    for (auto *widget : widgets) {
        QApplication::sendEvent(widget, &event);
        widget->update();
    }

    Q_EMIT ModeChanged(mode);
}

void ThemeController::ApplyTitleBars()
{
    const bool dark = Mode() == Theme::Mode::Dark;
    const auto windows = QApplication::topLevelWidgets();
    for (auto *widget : windows) {
        Core::WindowEffects::ApplyDarkTitleBar(widget->windowHandle(), dark);
    }
}

bool ThemeController::eventFilter(QObject *watched, QEvent *event)
{
    // A top-level widget getting its native window: a dialog about to be shown for the
    // first time. It is realised by now, so the compositor can be asked.
    if (event->type() == QEvent::WinIdChange && _applied.has_value()) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget != nullptr && widget->isWindow()) {
            Core::WindowEffects::ApplyDarkTitleBar(
                widget->windowHandle(), *_applied == Theme::Mode::Dark);
        }
    }
    return QObject::eventFilter(watched, event);
}

} // namespace Gui
