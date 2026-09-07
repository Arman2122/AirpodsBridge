// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QSize>
#include <QString>

#include "Source/Domain/Base.h"

namespace Gui::DeviceArtwork {

//
// Apple's own picture of a device, as shipped in the executable.
//
// The artwork is fetched from Apple's asset catalogue and converted by
// `Tools/apple_device_assets.py`, which also writes the manifest this reads. A colour ships
// with a transparent still and, for the colour a model is shown in by default, the card's
// loop as an animated WebP. See Docs/Artwork.md.
//
struct Artwork {
    QString still;      // resource path of the transparent still
    QString loop;       // resource path of the animated loop; empty when the colour has none
    QSize size;         // pixel size of both
    int fps{0};         // the loop's rate and length, so it can be paced by the clock;
    int frames{0};      // zero without a loop
    QString colourName; // Apple's name for the colour, "White", "Dark Grey"
    // Where the things in the picture are, left to right, as fractions of its width: the
    // buds and the case, or the one object of a one-piece device. The battery rings sit
    // under them.
    std::vector<qreal> anchors;

    bool operator==(const Artwork &) const = default;
};

//
// The manifest, indexed by Apple's product type and colour code.
//
// The colour code is the one the device broadcasts, so a device is shown in its own colour
// when Apple publishes that colour; otherwise in the model's default colour.
//
class Catalog
{
public:
    // Parses `catalog.json`. An empty or malformed document yields an empty catalogue,
    // which finds nothing; it never throws.
    explicit Catalog(const QByteArray &json);

    std::optional<Artwork>
    Find(Core::AirPods::Model model, std::optional<uint8_t> colour = std::nullopt) const;

    size_t ProductCount() const;

private:
    struct Product {
        uint8_t defaultColour{0};
        std::map<uint8_t, Artwork> variants;
    };

    std::map<QString, Product> _products;
};

// The catalogue compiled into the executable. Loaded on first use.
const Catalog &Shipped();

// Convenience over `Shipped()`.
std::optional<Artwork>
Find(Core::AirPods::Model model, std::optional<uint8_t> colour = std::nullopt);

} // namespace Gui::DeviceArtwork
