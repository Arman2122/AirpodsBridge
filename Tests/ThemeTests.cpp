// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

//
// Checks the two palettes in `Source/Presentation/Theme.h`.
//
// Nothing can assert that dark mode looks good. What can be asserted is that it is
// readable: every token is set, and text keeps the contrast the WCAG asks for on every
// surface it is drawn on, in dark as in light. A palette edit that loses that fails here
// rather than in a screenshot.
//

#include <QtTest>

#include <cmath>
#include <vector>

#include "Source/Presentation/Theme.h"

namespace {

using Gui::Theme::Palette;

struct Token {
    const char *name;
    QColor Palette::*colour;
};

// Every token, so a new one is checked as soon as it exists. Missing one here fails
// `everyTokenIsSet` only if it is also left unset, which is the case worth catching.
const std::vector<Token> kTokens{
    {"Surface", &Palette::Surface},
    {"Control", &Palette::Control},
    {"ControlHover", &Palette::ControlHover},
    {"ControlPressed", &Palette::ControlPressed},
    {"Raised", &Palette::Raised},
    {"Button", &Palette::Button},
    {"ButtonHover", &Palette::ButtonHover},
    {"ButtonPressed", &Palette::ButtonPressed},
    {"TextPrimary", &Palette::TextPrimary},
    {"TextSecondary", &Palette::TextSecondary},
    {"Glyph", &Palette::Glyph},
    {"BatteryGood", &Palette::BatteryGood},
    {"BatteryLow", &Palette::BatteryLow},
    {"BatteryTrack", &Palette::BatteryTrack},
    {"BatteryBolt", &Palette::BatteryBolt},
    {"Sidebar", &Palette::Sidebar},
    {"SidebarSelected", &Palette::SidebarSelected},
    {"Card", &Palette::Card},
    {"Divider", &Palette::Divider},
    {"Accent", &Palette::Accent},
    {"OnAccent", &Palette::OnAccent},
    {"KnobShadow", &Palette::KnobShadow},
    {"Focus", &Palette::Focus},
};

struct Named {
    const char *name;
    const Palette &palette;
};

const std::vector<Named> kPalettes{
    {"light", Gui::Theme::Light()},
    {"dark", Gui::Theme::Dark()},
};

// WCAG 2 relative luminance and contrast ratio.
qreal Luminance(const QColor &colour)
{
    const auto channel = [](qreal value) {
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.redF()) + 0.7152 * channel(colour.greenF()) +
           0.0722 * channel(colour.blueF());
}

qreal Contrast(const QColor &a, const QColor &b)
{
    const qreal la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Opaque only: the ratio of a translucent colour depends on what is under it.
QColor Opaque(const QColor &colour)
{
    QColor result = colour;
    result.setAlpha(255);
    return result;
}

#define VERIFY_CONTRAST(palette, name, fg, bg, minimum)                                            \
    do {                                                                                           \
        const qreal ratio = Contrast(Opaque(palette.fg), Opaque(palette.bg));                      \
        QVERIFY2(                                                                                  \
            ratio >= minimum, qPrintable(QStringLiteral("%1: %2 on %3 is %4:1, below %5:1")        \
                                             .arg(name, #fg, #bg)                                  \
                                             .arg(ratio, 0, 'f', 2)                                \
                                             .arg(minimum)));                                      \
    } while (false)

} // namespace

class ThemeTests : public QObject
{
    Q_OBJECT

private slots:
    void everyTokenIsSet();
    void lightIsLightAndDarkIsDark();
    void textIsReadableOnEverySurface();
    void controlsStandOutFromTheirSurface();
    void applicationPaletteFollowsThePalette();
    void modeSwitchesThePalette();
    void changeEventIsRegistered();
};

void ThemeTests::everyTokenIsSet()
{
    for (const auto &[name, palette] : kPalettes) {
        for (const auto &token : kTokens) {
            QVERIFY2(
                (palette.*token.colour).isValid(),
                qPrintable(QStringLiteral("%1: %2 is not set").arg(name, token.name)));
        }
    }
}

void ThemeTests::lightIsLightAndDarkIsDark()
{
    const auto &light = Gui::Theme::Light();
    const auto &dark = Gui::Theme::Dark();

    QVERIFY(Luminance(light.Surface) > 0.8);
    QVERIFY(Luminance(dark.Surface) < 0.05);
    QVERIFY(Luminance(light.TextPrimary) < Luminance(light.Surface));
    QVERIFY(Luminance(dark.TextPrimary) > Luminance(dark.Surface));

    // Dark is a design, not an inversion: the surfaces still step *up* from the window,
    // so a card reads as a layer on it, and the accent is Apple's brighter dark variant.
    QVERIFY(Luminance(dark.Card) > Luminance(dark.Surface));
    QVERIFY(Luminance(dark.Sidebar) > Luminance(dark.Surface));
    QVERIFY(Luminance(dark.Control) > Luminance(dark.Surface));
    QVERIFY(Luminance(dark.Accent) > Luminance(light.Accent));
}

void ThemeTests::textIsReadableOnEverySurface()
{
    // WCAG AA: 4.5:1 for text, 3:1 for large text and for the parts of a control that
    // have to be seen. The secondary text and the glyph grey are held to the latter; on
    // the surfaces they are actually used on both clear the former comfortably.
    for (const auto &[name, palette] : kPalettes) {
        VERIFY_CONTRAST(palette, name, TextPrimary, Surface, 4.5);
        VERIFY_CONTRAST(palette, name, TextPrimary, Card, 4.5);
        VERIFY_CONTRAST(palette, name, TextPrimary, Sidebar, 4.5);
        VERIFY_CONTRAST(palette, name, TextPrimary, SidebarSelected, 4.5);
        VERIFY_CONTRAST(palette, name, TextPrimary, Control, 4.5);
        VERIFY_CONTRAST(palette, name, TextPrimary, Button, 4.5);
        VERIFY_CONTRAST(palette, name, TextPrimary, Raised, 4.5);

        VERIFY_CONTRAST(palette, name, TextSecondary, Surface, 4.5);
        VERIFY_CONTRAST(palette, name, TextSecondary, Card, 4.5);

        VERIFY_CONTRAST(palette, name, Glyph, Surface, 3.0);
        VERIFY_CONTRAST(palette, name, Glyph, Card, 3.0);
        VERIFY_CONTRAST(palette, name, Glyph, Control, 3.0);

        VERIFY_CONTRAST(palette, name, OnAccent, Accent, 3.0);
    }
}

void ThemeTests::controlsStandOutFromTheirSurface()
{
    for (const auto &[name, palette] : kPalettes) {
        // What is *on* has to be visible against what it sits on.
        VERIFY_CONTRAST(palette, name, Accent, Surface, 3.0);
        VERIFY_CONTRAST(palette, name, Accent, Card, 3.0);
        VERIFY_CONTRAST(palette, name, BatteryGood, Surface, 2.0);
        VERIFY_CONTRAST(palette, name, BatteryLow, Surface, 2.0);

        // A control at rest and the surface it sits on are different colours, and its
        // hover and pressed states differ from rest, or the states are invisible.
        QVERIFY2(palette.Control != palette.Surface, name);
        QVERIFY2(palette.ControlHover != palette.Control, name);
        QVERIFY2(palette.ControlPressed != palette.ControlHover, name);
        QVERIFY2(palette.ButtonHover != palette.Button, name);
        QVERIFY2(palette.ButtonPressed != palette.Button, name);
        QVERIFY2(palette.Raised != palette.Control, name);
        QVERIFY2(palette.Divider != palette.Card, name);
        QVERIFY2(palette.SidebarSelected != palette.Sidebar, name);
    }
}

void ThemeTests::applicationPaletteFollowsThePalette()
{
    for (const auto &[name, palette] : kPalettes) {
        const auto application = Gui::Theme::ApplicationPalette(palette);

        QCOMPARE(application.color(QPalette::Window), palette.Surface);
        QCOMPARE(application.color(QPalette::WindowText), palette.TextPrimary);
        QCOMPARE(application.color(QPalette::Base), palette.Surface);
        QCOMPARE(application.color(QPalette::Text), palette.TextPrimary);
        QCOMPARE(application.color(QPalette::Button), palette.Control);
        QCOMPARE(application.color(QPalette::ButtonText), palette.TextPrimary);
        QCOMPARE(application.color(QPalette::Highlight), palette.Accent);
        QCOMPARE(application.color(QPalette::HighlightedText), palette.OnAccent);
        QCOMPARE(application.color(QPalette::Link), palette.Accent);
        QCOMPARE(application.color(QPalette::Disabled, QPalette::WindowText), palette.Glyph);
        QCOMPARE(application.color(QPalette::Disabled, QPalette::ButtonText), palette.Glyph);
        // The Inactive group is what an unfocused window draws with; it must not fall
        // back to Qt's defaults.
        QCOMPARE(application.color(QPalette::Inactive, QPalette::Window), palette.Surface);
        QCOMPARE(application.color(QPalette::Inactive, QPalette::Text), palette.TextPrimary);
    }
}

void ThemeTests::modeSwitchesThePalette()
{
    using Gui::Theme::Mode;

    Gui::Theme::SetMode(Mode::Light);
    QCOMPARE(Gui::Theme::CurrentMode(), Mode::Light);
    QCOMPARE(&Gui::Theme::Colours(), &Gui::Theme::Light());

    Gui::Theme::SetMode(Mode::Dark);
    QCOMPARE(Gui::Theme::CurrentMode(), Mode::Dark);
    QCOMPARE(&Gui::Theme::Colours(), &Gui::Theme::Dark());
    QCOMPARE(&Gui::Theme::PaletteFor(Mode::Light), &Gui::Theme::Light());

    Gui::Theme::SetMode(Mode::Light);
}

void ThemeTests::changeEventIsRegistered()
{
    const auto type = Gui::Theme::ChangeEvent();
    QVERIFY(type >= QEvent::User);
    QCOMPARE(Gui::Theme::ChangeEvent(), type);
}

QTEST_MAIN(ThemeTests)
#include "ThemeTests.moc"
