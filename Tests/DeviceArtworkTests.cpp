// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

//
// Checks the device artwork the application ships: Apple's own renders, fetched and
// converted by `Tools/apple_device_assets.py` into `Source/Resource/Device/`.
//
// The catalogue is read from the source tree rather than the qrc, so a model whose artwork
// was never fetched, or a file the qrc does not list, fails here instead of shipping as a
// blank popup.
//

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "Source/Domain/Base.h"
#include "Source/Presentation/DeviceArtwork.h"
#include "Source/Support/Helper.h"

namespace {

const QString kDeviceDir = QString{APB_SOURCE_DIR} + "/Source/Resource/Device";

QJsonObject LoadCatalog()
{
    QFile file{kDeviceDir + "/catalog.json"};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString LoadQrc()
{
    QFile file{kDeviceDir + "/Device.qrc"};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// Every real model: `Unknown` and the `_Max` sentinel excluded.
std::vector<Core::AirPods::Model> RealModels()
{
    std::vector<Core::AirPods::Model> models;
    for (uint32_t i = 1; i < Helper::ToUnderlying(Core::AirPods::Model::_Max); ++i) {
        models.push_back(static_cast<Core::AirPods::Model>(i));
    }
    return models;
}

} // namespace

class DeviceArtworkTests : public QObject
{
    Q_OBJECT

private slots:
    void everyModelNamesAnAppleProduct();
    void catalogCoversEveryModel();
    void referencedFilesExistAndAreListed();
    void loopsAreAnimatedWebpWithAlpha();
    void anchorsMatchWhatTheDeviceIs();
    void findsTheExactColourAndFallsBackToTheDefault();
    void shippedCatalogMatchesTheSourceTree();
};

void DeviceArtworkTests::everyModelNamesAnAppleProduct()
{
    QVERIFY(Core::AirPods::AppleProductType(Core::AirPods::Model::Unknown).isEmpty());

    const QRegularExpression shape{"^[A-Za-z]+\\d+,\\d+$"};
    for (const auto model : RealModels()) {
        const auto product = Core::AirPods::AppleProductType(model);
        QVERIFY2(
            shape.match(product).hasMatch(),
            qPrintable(QString{"%1 maps to '%2'"}.arg(Helper::ToString(model), product)));
    }
}

void DeviceArtworkTests::catalogCoversEveryModel()
{
    const auto catalog = LoadCatalog();
    QVERIFY2(!catalog.isEmpty(), "catalog.json is missing or not an object");
    const auto products = catalog.value("products").toObject();
    QVERIFY(!products.isEmpty());

    for (const auto model : RealModels()) {
        const auto productType = Core::AirPods::AppleProductType(model);
        const auto product = products.value(productType).toObject();
        QVERIFY2(
            !product.isEmpty(), qPrintable(QString{"no artwork for %1 (%2)"}.arg(
                                    Helper::ToString(model), productType)));

        const auto defaultCl = QString::number(product.value("default").toInt(-1));
        const auto variant = product.value("variants").toObject().value(defaultCl).toObject();
        QVERIFY2(
            variant.contains("still"),
            qPrintable(QString{"%1: default colour %2 has no still"}.arg(productType, defaultCl)));
    }
}

void DeviceArtworkTests::referencedFilesExistAndAreListed()
{
    const auto products = LoadCatalog().value("products").toObject();
    const auto qrc = LoadQrc();
    QVERIFY(!qrc.isEmpty());
    QVERIFY(qrc.contains("<file>catalog.json</file>"));

    for (auto it = products.begin(); it != products.end(); ++it) {
        const auto variants = it.value().toObject().value("variants").toObject();
        QVERIFY(!variants.isEmpty());
        for (auto vt = variants.begin(); vt != variants.end(); ++vt) {
            const auto variant = vt.value().toObject();
            for (const auto key : {"still", "loop"}) {
                if (!variant.contains(key)) {
                    continue;
                }
                const auto relative = variant.value(key).toString();
                QVERIFY2(
                    QFile::exists(kDeviceDir + "/" + relative),
                    qPrintable(QString{"missing file: %1"}.arg(relative)));
                QVERIFY2(
                    qrc.contains(QString{"<file>%1</file>"}.arg(relative)),
                    qPrintable(QString{"not listed in Device.qrc: %1"}.arg(relative)));
            }
        }
    }
}

void DeviceArtworkTests::loopsAreAnimatedWebpWithAlpha()
{
    const auto products = LoadCatalog().value("products").toObject();
    int loops = 0;

    for (auto it = products.begin(); it != products.end(); ++it) {
        const auto variants = it.value().toObject().value("variants").toObject();
        for (auto vt = variants.begin(); vt != variants.end(); ++vt) {
            const auto variant = vt.value().toObject();
            if (!variant.contains("loop")) {
                continue;
            }
            ++loops;
            const auto path = kDeviceDir + "/" + variant.value("loop").toString();
            const auto expectedFps = variant.value("fps").toInt();
            const auto size = variant.value("size").toArray();

            QImageReader reader{path};
            QVERIFY2(reader.canRead(), qPrintable(path + ": " + reader.errorString()));
            QVERIFY2(reader.supportsAnimation(), qPrintable(path + ": not an animation"));
            QVERIFY2(reader.imageCount() > 1, qPrintable(path + ": a single frame"));
            // The view paces the loop from the catalogue's frame count, so it must be right.
            QCOMPARE(reader.imageCount(), variant.value("frames").toInt());
            QCOMPARE(reader.size(), QSize(size.at(0).toInt(), size.at(1).toInt()));

            QImage frame;
            for (int i = 0; i <= 10 && i < reader.imageCount(); ++i) {
                frame = reader.read();
                QVERIFY2(!frame.isNull(), qPrintable(path + ": " + reader.errorString()));
                if (i == 0) {
                    // Within a frame of the catalogue's rate; the file stores whole
                    // milliseconds.
                    QVERIFY2(
                        std::abs(reader.nextImageDelay() - 1000 / expectedFps) <= 1,
                        qPrintable(QString{"%1: frame delay %2 ms"}.arg(path).arg(
                            reader.nextImageDelay())));
                }
            }
            QVERIFY2(frame.hasAlphaChannel(), qPrintable(path + ": no alpha channel"));
            // Apple's renders float on nothing: the corner must be fully transparent.
            QCOMPARE(qAlpha(frame.convertToFormat(QImage::Format_ARGB32).pixel(1, 1)), 0);
        }
    }
    QVERIFY2(loops > 0, "the catalogue references no animations");
}

void DeviceArtworkTests::anchorsMatchWhatTheDeviceIs()
{
    // The popup sits the battery rings under the artwork's anchors, so every variant
    // needs them, in order, inside the picture; and the count has to agree with what the
    // domain says the device is -- two things for a pair with a case, one for AirPods Max.
    QFile file{kDeviceDir + "/catalog.json"};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const Gui::DeviceArtwork::Catalog catalog{file.readAll()};

    const auto products = LoadCatalog().value("products").toObject();
    for (auto it = products.begin(); it != products.end(); ++it) {
        const auto variants = it.value().toObject().value("variants").toObject();
        for (auto vt = variants.begin(); vt != variants.end(); ++vt) {
            const auto anchors = vt.value().toObject().value("anchors").toArray();
            const auto label = QStringLiteral("%1 CL %2").arg(it.key(), vt.key());
            QVERIFY2(anchors.size() == 1 || anchors.size() == 2, qPrintable(label));
            qreal previous = 0.0;
            for (const auto &anchor : anchors) {
                const auto value = anchor.toDouble(-1.0);
                QVERIFY2(value > previous && value < 1.0, qPrintable(label));
                previous = value;
            }
        }
    }

    for (const auto model : RealModels()) {
        const auto artwork = catalog.Find(model);
        QVERIFY(artwork.has_value());
        const auto expected = Core::AirPods::IsOnePiece(model) ? size_t{1} : size_t{2};
        QVERIFY2(
            artwork->anchors.size() == expected, qPrintable(QString{"%1: %2 anchors"}
                                                                .arg(Helper::ToString(model))
                                                                .arg(artwork->anchors.size())));
    }
}

void DeviceArtworkTests::findsTheExactColourAndFallsBackToTheDefault()
{
    QFile file{kDeviceDir + "/catalog.json"};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const Gui::DeviceArtwork::Catalog catalog{file.readAll()};
    using Core::AirPods::Model;

    // Apple publishes the USB-C AirPods Max in Blue as colour 20; 99 is nobody's colour.
    const auto blue = catalog.Find(Model::AirPods_Max_USB_C, 20);
    QVERIFY(blue.has_value());
    QCOMPARE(blue->colourName, QString{"Blue"});
    QVERIFY(blue->still.startsWith(":/Resource/Device/AirPodsMax1_2/CL_20/"));

    const auto fallback = catalog.Find(Model::AirPods_Max_USB_C, 99);
    QVERIFY(fallback.has_value());
    QCOMPARE(fallback, catalog.Find(Model::AirPods_Max_USB_C));
    // The default colour is the one that ships the animation, at Apple's own rate.
    QVERIFY(!fallback->loop.isEmpty());
    QCOMPARE(fallback->fps, 60);
    QVERIFY(fallback->frames > 0);
    QCOMPARE(blue->fps, 0);

    // Two models that share a bundle share the artwork.
    QCOMPARE(catalog.Find(Model::AirPods_Pro_2_USB_C), catalog.Find(Model::AirPods_Pro_2));

    QVERIFY(!catalog.Find(Model::Unknown).has_value());
    QVERIFY(!Gui::DeviceArtwork::Catalog{QByteArray{}}.Find(Model::AirPods_1).has_value());
}

void DeviceArtworkTests::shippedCatalogMatchesTheSourceTree()
{
    const auto products = LoadCatalog().value("products").toObject();
    QCOMPARE(Gui::DeviceArtwork::Shipped().ProductCount(), static_cast<size_t>(products.size()));

    // What the compiled-in catalogue names is really compiled in.
    const auto artwork = Gui::DeviceArtwork::Find(Core::AirPods::Model::AirPods_Pro_3);
    QVERIFY(artwork.has_value());
    QVERIFY(QFile::exists(artwork->still));
    QVERIFY(artwork->loop.isEmpty() || QFile::exists(artwork->loop));
}

QTEST_MAIN(DeviceArtworkTests)
#include "DeviceArtworkTests.moc"
