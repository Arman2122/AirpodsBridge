// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Presentation/Theme.h"

#include <array>

#include <QFontDatabase>
#include <QString>
#include <QStringList>

#include "Source/Support/Logger.h"

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

QFont Make(const QString &family, int pointSize, QFont::Weight weight, qreal tracking)
{
    QFont font{family, pointSize, weight};
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

} // namespace Gui::Theme::Font
