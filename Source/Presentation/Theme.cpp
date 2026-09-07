// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Presentation/Theme.h"

#include <array>

#include <QFontDatabase>
#include <QString>
#include <QStringList>

#include "Source/Support/Logger.h"

namespace Gui::Theme {

namespace {

// clang-format off

//
// Light: the popup as it was first designed, white under white products.
//
const Palette kLight{
    .Surface         = QColor{255, 255, 255},
    .Control         = QColor{238, 238, 239},
    .ControlHover    = QColor{228, 228, 229},
    .ControlPressed  = QColor{218, 218, 219},
    .Raised          = QColor{255, 255, 255},

    .Button          = QColor{212, 211, 217},
    .ButtonHover     = QColor{222, 221, 227},
    .ButtonPressed   = QColor{202, 201, 207},

    .TextPrimary     = QColor{32, 32, 32},
    .TextSecondary   = QColor{94, 94, 94},
    .Glyph           = QColor{131, 131, 135},

    .BatteryGood     = QColor{52, 199, 89},
    .BatteryLow      = QColor{255, 59, 48},
    .BatteryTrack    = QColor{229, 229, 234},
    .BatteryBolt     = QColor{255, 255, 255},

    .Sidebar         = QColor{246, 246, 247},
    .SidebarSelected = QColor{225, 225, 228},
    .Card            = QColor{246, 246, 247},
    .Divider         = QColor{229, 229, 234},

    .Accent          = QColor{0, 122, 255},
    .OnAccent        = QColor{255, 255, 255},
    .KnobShadow      = QColor{0, 0, 0, 28},
    .Focus           = QColor{0, 122, 255, 110},
};

//
// Dark: designed, not inverted. Surfaces step up from a near-black rather than down from
// white, so the cards and the sidebar still read as a layer *on* the window. The accent
// and the battery colours are Apple's dark-mode variants, a shade brighter because they
// sit on dark. The text is off-white -- pure white on near-black glares -- and the knob
// shadow is heavier, because a shadow has to be to show on dark at all. Every text token
// keeps at least the contrast it has in light; Tests/ThemeTests.cpp holds both to that.
//
const Palette kDark{
    .Surface         = QColor{30, 30, 32},
    .Control         = QColor{56, 56, 60},
    .ControlHover    = QColor{68, 68, 72},
    .ControlPressed  = QColor{82, 82, 86},
    .Raised          = QColor{104, 104, 110},

    .Button          = QColor{74, 74, 79},
    .ButtonHover     = QColor{88, 88, 93},
    .ButtonPressed   = QColor{62, 62, 67},

    .TextPrimary     = QColor{236, 236, 240},
    .TextSecondary   = QColor{162, 162, 170},
    .Glyph           = QColor{140, 140, 148},

    .BatteryGood     = QColor{48, 209, 88},
    .BatteryLow      = QColor{255, 69, 58},
    .BatteryTrack    = QColor{62, 62, 66},
    .BatteryBolt     = QColor{255, 255, 255},

    .Sidebar         = QColor{38, 38, 41},
    .SidebarSelected = QColor{72, 72, 77},
    .Card            = QColor{43, 43, 46},
    .Divider         = QColor{62, 62, 66},

    .Accent          = QColor{10, 132, 255},
    .OnAccent        = QColor{255, 255, 255},
    .KnobShadow      = QColor{0, 0, 0, 90},
    .Focus           = QColor{10, 132, 255, 130},
};

// clang-format on

Mode g_mode = Mode::Light;

} // namespace

const Palette &Light()
{
    return kLight;
}

const Palette &Dark()
{
    return kDark;
}

const Palette &PaletteFor(Mode mode)
{
    return mode == Mode::Dark ? kDark : kLight;
}

const Palette &Colours()
{
    return PaletteFor(g_mode);
}

Mode CurrentMode()
{
    return g_mode;
}

void SetMode(Mode mode)
{
    g_mode = mode;
}

QPalette ApplicationPalette(const Palette &palette)
{
    QPalette result;

    // Setting a role without a group sets it for Active, Inactive and Disabled alike.
    result.setColor(QPalette::Window, palette.Surface);
    result.setColor(QPalette::WindowText, palette.TextPrimary);
    result.setColor(QPalette::Base, palette.Surface);
    result.setColor(QPalette::AlternateBase, palette.Card);
    result.setColor(QPalette::Text, palette.TextPrimary);
    result.setColor(QPalette::PlaceholderText, palette.TextSecondary);
    result.setColor(QPalette::Button, palette.Control);
    result.setColor(QPalette::ButtonText, palette.TextPrimary);
    result.setColor(QPalette::BrightText, palette.OnAccent);
    result.setColor(QPalette::Highlight, palette.Accent);
    result.setColor(QPalette::HighlightedText, palette.OnAccent);
    result.setColor(QPalette::Link, palette.Accent);
    result.setColor(QPalette::LinkVisited, palette.Accent);
    result.setColor(QPalette::ToolTipBase, palette.Card);
    result.setColor(QPalette::ToolTipText, palette.TextPrimary);

    // The bevel roles Fusion draws frames, separators and outlines with, ordered from the
    // lightest edge to the darkest, in both modes.
    result.setColor(QPalette::Light, palette.Raised);
    result.setColor(QPalette::Midlight, palette.Control);
    result.setColor(QPalette::Mid, palette.Divider);
    result.setColor(QPalette::Dark, palette.Divider.darker(150));
    result.setColor(QPalette::Shadow, QColor{0, 0, 0});

    // Disabled text is the glyph grey everywhere, as it is in the painted controls.
    for (const auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::HighlightedText})
    {
        result.setColor(QPalette::Disabled, role, palette.Glyph);
    }
    result.setColor(QPalette::Disabled, QPalette::Highlight, palette.Divider);

    return result;
}

QEvent::Type ChangeEvent()
{
    static const auto type = static_cast<QEvent::Type>(QEvent::registerEventType());
    return type;
}

} // namespace Gui::Theme

namespace Gui::Theme::Font {
namespace {

//
// Resolved once by `Register()` and read by the accessors below.
//
QString g_textFamily;
QString g_displayFamily;

//
// Preference order. SF Pro comes first because a machine that has it should use the real
// face, but it cannot be shipped: Apple's font licence covers development for Apple
// platforms and forbids redistribution. Inter is bundled instead -- it is SIL OFL, and it
// was drawn for the same job, so the two sit at a similar optical weight and width.
//
// Segoe UI is the last resort rather than the first: if neither of the above is present,
// matching the host platform is better than falling through to whatever Qt picks.
//
constexpr std::array kTextPreference{"SF Pro Text", "SF Pro Display", "Inter", "Segoe UI"};
constexpr std::array kDisplayPreference{"SF Pro Display", "Inter Display", "Inter", "Segoe UI"};

QString FirstAvailable(const QStringList &installed, const auto &preference)
{
    for (const auto *candidate : preference) {
        if (installed.contains(QString::fromLatin1(candidate), Qt::CaseInsensitive)) {
            return QString::fromLatin1(candidate);
        }
    }
    return {};
}

QFont Make(const QString &family, qreal pointSize, QFont::Weight weight, qreal tracking)
{
    QFont font{family};
    font.setPointSizeF(pointSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    // Tracking is expressed as a percentage of the natural advance, so 100 is unchanged.
    font.setLetterSpacing(QFont::PercentageSpacing, tracking);
    return font;
}

} // namespace

void Register()
{
    static constexpr std::array kBundled{
        ":/Resource/Font/Inter-Regular.ttf",
        ":/Resource/Font/Inter-Medium.ttf",
        ":/Resource/Font/Inter-SemiBold.ttf",
        ":/Resource/Font/InterDisplay-SemiBold.ttf",
    };

    for (const auto *path : kBundled) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(path)) < 0) {
            LOG(Warn, "Theme: failed to register bundled font '{}'.", path);
        }
    }

    const auto installed = QFontDatabase{}.families();
    g_textFamily = FirstAvailable(installed, kTextPreference);
    g_displayFamily = FirstAvailable(installed, kDisplayPreference);

    LOG(Info, "Theme: text family '{}', display family '{}'.", g_textFamily, g_displayFamily);
}

QString TextFamily()
{
    return g_textFamily;
}

QString DisplayFamily()
{
    return g_displayFamily;
}

QFont Title()
{
    return Make(g_displayFamily, 16, QFont::Bold, 97);
}

QFont Body()
{
    return Make(g_textFamily, 9, QFont::Normal, 100);
}

QFont Caption()
{
    return Make(g_textFamily, 9, QFont::Medium, 100);
}

QFont Button()
{
    return Make(g_textFamily, 10, QFont::DemiBold, 100);
}

QFont Heading()
{
    return Make(g_displayFamily, 15, QFont::DemiBold, 98);
}

QFont Footnote()
{
    return Make(g_textFamily, 8.5, QFont::Normal, 100);
}

} // namespace Gui::Theme::Font
