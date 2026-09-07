// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Presentation/DeviceArtwork.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Gui::DeviceArtwork {

namespace {

const QString kResourcePrefix = QStringLiteral(":/Resource/Device/");

} // namespace

Catalog::Catalog(const QByteArray &json)
{
    const auto products = QJsonDocument::fromJson(json).object().value("products").toObject();

    for (auto it = products.begin(); it != products.end(); ++it) {
        const auto object = it.value().toObject();
        Product product;
        product.defaultColour = static_cast<uint8_t>(object.value("default").toInt());

        const auto variants = object.value("variants").toObject();
        for (auto vt = variants.begin(); vt != variants.end(); ++vt) {
            const auto variant = vt.value().toObject();
            bool numeric = false;
            const auto colour = vt.key().toUInt(&numeric);
            if (!numeric || colour > 0xFF || !variant.contains("still")) {
                continue;
            }

            Artwork artwork;
            artwork.still = kResourcePrefix + variant.value("still").toString();
            if (variant.contains("loop")) {
                artwork.loop = kResourcePrefix + variant.value("loop").toString();
                artwork.fps = variant.value("fps").toInt();
                artwork.frames = variant.value("frames").toInt();
            }
            const auto size = variant.value("size").toArray();
            artwork.size = QSize{size.at(0).toInt(), size.at(1).toInt()};
            artwork.colourName = variant.value("name").toString();
            const auto anchors = variant.value("anchors").toArray();
            for (const auto &anchor : anchors) {
                artwork.anchors.push_back(anchor.toDouble());
            }
            product.variants.emplace(static_cast<uint8_t>(colour), std::move(artwork));
        }

        if (!product.variants.empty()) {
            _products.emplace(it.key(), std::move(product));
        }
    }
}

std::optional<Artwork>
Catalog::Find(Core::AirPods::Model model, std::optional<uint8_t> colour) const
{
    const auto product = _products.find(Core::AirPods::AppleProductType(model));
    if (product == _products.end()) {
        return std::nullopt;
    }

    const auto &variants = product->second.variants;
    if (colour.has_value()) {
        if (const auto exact = variants.find(*colour); exact != variants.end()) {
            return exact->second;
        }
    }
    if (const auto fallback = variants.find(product->second.defaultColour);
        fallback != variants.end())
    {
        return fallback->second;
    }
    return variants.begin()->second;
}

size_t Catalog::ProductCount() const
{
    return _products.size();
}

const Catalog &Shipped()
{
    static const Catalog catalog = [] {
        QFile file{kResourcePrefix + "catalog.json"};
        file.open(QIODevice::ReadOnly);
        return Catalog{file.readAll()};
    }();
    return catalog;
}

std::optional<Artwork> Find(Core::AirPods::Model model, std::optional<uint8_t> colour)
{
    return Shipped().Find(model, colour);
}

} // namespace Gui::DeviceArtwork
