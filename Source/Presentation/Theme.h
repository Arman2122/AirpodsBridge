// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <QColor>
#include <QFont>

class QString;

namespace Gui::Theme {

//
// The single source of truth for the application's colours and metrics.
//
// The palette is deliberately fixed and light. It is not a bug that it does not follow the
// Windows light/dark setting: the device animations in `Source/Resource/Video/` are
// composited onto white, and the products themselves are white, so a dark surface would
// leave the artwork sitting in a bright rectangle. Fixing the palette is a product
// decision, not an omission -- but the values still belong in one place rather than
// scattered as literals through the widgets.
//
// Greys are neutral rather than blue-tinted so they do not fight the warm white of the
// animation frames.
//

namespace Colour {

// Surfaces
inline const QColor Surface{255, 255, 255};        // the window itself
inline const QColor Control{238, 238, 239};        // a control at rest
inline const QColor ControlHover{228, 228, 229};   // pointer over it
inline const QColor ControlPressed{218, 218, 219}; // pointer down on it

// The primary action button sits on the surface and reads as slightly heavier than the
// close button, so it has its own ramp rather than reusing Control*.
inline const QColor Button{212, 211, 217};
inline const QColor ButtonHover{222, 221, 227};
inline const QColor ButtonPressed{202, 201, 207};

// Content
inline const QColor TextPrimary{32, 32, 32};
inline const QColor TextSecondary{94, 94, 94};
inline const QColor Glyph{131, 131, 135}; // close cross, and other line art

// Battery, following Apple's system colours so the indicator reads the way the same
// information does on an iPhone.
inline const QColor BatteryGood{52, 199, 89};    // systemGreen
inline const QColor BatteryLow{255, 59, 48};     // systemRed
inline const QColor BatteryTrack{229, 229, 234}; // systemGray5, the unfilled ring
inline const QColor BatteryOutline{0, 0, 0, 90}; // label at 35%
inline const QColor BatteryBolt{255, 255, 255};  // knocked out of the fill while charging

} // namespace Colour

namespace Metric {

// What the compositor gives us. DWM offers only a fixed 8px "round" and a 4px
// "round small"; it cannot be asked for a radius. Painting a larger one needs a
// translucent window, which conflicts with both QVideoWidget and DWM's own rounding.
inline constexpr qreal WindowCornerRadius = 8.0;
inline constexpr qreal ControlCornerRadius = 8.0;

inline constexpr int CloseButtonSize = 26;
// The glyph is inset to about a third of the button and stroked thin, which is the
// proportion iOS uses; a heavier cross reads as a warning rather than a dismiss.
inline constexpr qreal CloseButtonGlyphInset = 0.34;
inline constexpr qreal CloseButtonGlyphWidth = 1.9;

} // namespace Metric

namespace Font {

//
// Register the bundled faces. Call once, before any widget is constructed.
//
// The family actually used is resolved at that point: SF Pro is preferred when the machine
// has it, because that is the real thing, and Inter is used otherwise. Inter is bundled
// under the SIL Open Font License and is a deliberate stand-in -- SF Pro cannot legally be
// redistributed with a Windows application, so shipping it is not an option.
//
void Register();

// The resolved family names, valid after Register().
QString TextFamily();
QString DisplayFamily();

//
// The type scale. Sizes are in points and the tracking follows the same idea Apple uses:
// tighter as the type gets larger, so headings do not read as loose.
//
QFont Title();   // the device name
QFont Body();    // default interface text
QFont Caption(); // battery percentages and other secondary text
QFont Button();  // the action button

} // namespace Font

namespace Duration {

// Preferred lengths in milliseconds. These are targets, not final values: pass them
// through `Core::WindowEffects::AnimationDuration` so they land on a whole number of the
// display's frames and collapse to zero when the user has turned animations off.
inline constexpr int WindowMove = 250;

} // namespace Duration

} // namespace Gui::Theme
