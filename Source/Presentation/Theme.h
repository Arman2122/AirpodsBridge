// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QColor>
#include <QEvent>
#include <QFont>
#include <QPalette>

class QString;

namespace Gui::Theme {

//
// The single source of truth for the application's colours, metrics and type.
//
// There are two palettes, light and dark, with the same tokens, and `Colours()` is
// whichever is in force. Which one that is follows the appearance setting and Windows'
// own mode; `Gui::ThemeController` decides and switches, and this file only holds the
// values. A dark surface became possible when the device artwork became transparent
// (`Source/Resource/Device/`): the animations it replaced were composited onto white.
//
// Read a colour at the point of use, when painting or when building a stylesheet, and
// never keep one in a member: the palette changes under a running window. Anything that
// has to bake a colour in -- a stylesheet, a QPalette role -- is sent `ChangeEvent()`
// when that happens, and redoes it then.
//
// Greys are neutral rather than blue-tinted so they sit quietly under white products.
//

enum class Mode { Light, Dark };

struct Palette {
    // Surfaces
    QColor Surface;        // the window itself
    QColor Control;        // a control at rest
    QColor ControlHover;   // pointer over it
    QColor ControlPressed; // pointer down on it
    QColor Raised;         // what sits proud of a control: the chosen segment's pill

    // The primary action button sits on the surface and reads as slightly heavier than
    // the close button, so it has its own ramp rather than reusing Control*.
    QColor Button;
    QColor ButtonHover;
    QColor ButtonPressed;

    // Content
    QColor TextPrimary;
    QColor TextSecondary;
    QColor Glyph; // close cross, chevrons, disabled text, and other line art

    // Battery, following Apple's system colours so the indicator reads the way the same
    // information does on an iPhone.
    QColor BatteryGood;  // systemGreen
    QColor BatteryLow;   // systemRed
    QColor BatteryTrack; // systemGray5, the unfilled ring
    QColor BatteryBolt;  // knocked out of the fill while charging

    // Settings window. The navigation column and the cards share one tint, a step off
    // the surface, so a page reads as two layers rather than a grid of boxes.
    QColor Sidebar;
    QColor SidebarSelected;
    QColor Card;
    QColor Divider; // systemGray5: the hairline between rows

    // The accent is Apple's systemBlue, used only where something is *on*: a switch, the
    // filled part of a slider, a link. Battery keeps its own green and red above.
    QColor Accent;
    QColor OnAccent;   // a knob, and anything drawn over Accent
    QColor KnobShadow; // what lifts a knob off its track
    QColor Focus;      // the keyboard focus ring
};

const Palette &Light();
const Palette &Dark();
const Palette &PaletteFor(Mode mode);

// The palette in force. Read it when painting; do not keep what it returns.
const Palette &Colours();
Mode CurrentMode();

// Swaps the palette. `Gui::ThemeController` is the caller: it also resets the application
// palette and tells every widget, which this alone does not.
void SetMode(Mode mode);

//
// The QPalette for the widgets this project does not paint itself -- message boxes, the
// tray menu, the device chooser -- built from one of the palettes above. Meant for the
// Fusion style, which draws entirely from its palette; the Windows style paints buttons
// and menus through the system theme and ignores most of this.
//
QPalette ApplicationPalette(const Palette &palette);

//
// Sent to every widget after the mode changed and the application palette was reset.
// A widget that bakes colours into a stylesheet or into QPalette roles handles it by
// doing that again; one that reads `Colours()` when it paints needs nothing, it is
// repainted.
//
QEvent::Type ChangeEvent();

namespace Metric {

// What the compositor gives us. DWM offers only a fixed 8px "round" and a 4px
// "round small"; it cannot be asked for a radius. Painting a larger one needs a
// translucent window, which conflicts with DWM's own rounding.
inline constexpr qreal WindowCornerRadius = 8.0;
inline constexpr qreal ControlCornerRadius = 8.0;

inline constexpr int CloseButtonSize = 26;
// The glyph is inset to about a third of the button and stroked thin, which is the
// proportion iOS uses; a heavier cross reads as a warning rather than a dismiss.
inline constexpr qreal CloseButtonGlyphInset = 0.34;
inline constexpr qreal CloseButtonGlyphWidth = 1.9;

// The two pod rings sit under the buds with clear air between them, as on the iPhone's
// card; the layout's default gap put their labels nearly touching.
inline constexpr int PodRingGap = 16;

// Settings window
inline constexpr int SidebarWidth = 172;
inline constexpr int PagePadding = 24;
inline constexpr int ScrollLine = 40; // one "line" of the system's wheel setting
inline constexpr qreal CardCornerRadius = 10.0;
inline constexpr int RowMinHeight = 48;
inline constexpr int RowPaddingH = 14;
inline constexpr int RowPaddingV = 10;

// Controls. The toggle's proportions are the iOS switch scaled to the type size here.
inline constexpr int ToggleWidth = 40;
inline constexpr int ToggleHeight = 24;
inline constexpr int SegmentHeight = 28;
inline constexpr qreal SegmentCornerRadius = 7.0;
inline constexpr int SliderTrackHeight = 4;
inline constexpr int SliderKnobDiameter = 18;

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
QFont Title();    // the device name
QFont Body();     // default interface text
QFont Caption();  // battery percentages and other secondary text
QFont Button();   // the action button
QFont Heading();  // a page title in the settings window
QFont Footnote(); // the line under a setting that explains it

} // namespace Font

namespace Duration {

// Preferred lengths in milliseconds. These are targets, not final values: pass them
// through `Core::WindowEffects::AnimationDuration` so they land on a whole number of the
// display's frames and collapse to zero when the user has turned animations off. The
// frames themselves come from `Gui::FrameClock`, one per vertical blank of the display.
inline constexpr int WindowMove = 250;
inline constexpr int Toggle = 160;
inline constexpr int Scroll = 240;

} // namespace Duration

} // namespace Gui::Theme
