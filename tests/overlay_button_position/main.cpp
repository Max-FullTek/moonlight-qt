#include "streaming/video/overlaybuttonposition.h"
#include "settings/devicelocalsettings.h"
#include "streaming/video/overlayplacementaxis.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cmath>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) {
        qFatal("%s", message);
    }
}

bool nearlyEqual(qreal lhs, qreal rhs)
{
    return std::abs(lhs - rhs) < 0.0001;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    const QRect landscape(100, 200, 1000, 600);

    // Compare native arithmetic with the prior Qt formulas, including negative
    // monitor origins, collapsed ranges, clipping, and half-pixel rounding.
    for (int origin : {-1920, -300, 0, 100, 3840}) {
        for (int parentLength : {0, 24, 36, 601, 1080, 3840}) {
            for (int testMargin : {-10, 0, 10, 9999}) {
                const int movable = qMax(0, parentLength - 36);
                const int safeMargin = qMin(qMax(0, testMargin), movable / 2);
                const int start = origin + safeMargin;
                const int end = origin + movable - safeMargin;
                const auto range = OverlayPlacementAxis::bounds(origin, parentLength, 36, testMargin);
                require(range.start == start && range.end == end, "native bounds match Qt");
                for (double value : {-2.0, 0.0, 0.25, 0.5, 0.75, 1.0, 4.0, qInf(), qQNaN()}) {
                    const double safe = qIsFinite(value) ? qBound(0.0, value, 1.0) : 0.5;
                    require(OverlayPlacementAxis::resolve(value, range) ==
                            qRound(start + (end - start) * safe), "native rounding matches Qt");
                }
                for (int pixel : {origin - 100, origin, origin + 50, origin + 4000}) {
                    const int clamped = qBound(start, pixel, end);
                    const double expected = end > start
                            ? qBound(0.0, static_cast<double>(clamped - start) / (end - start), 1.0)
                            : 0.5;
                    require(OverlayPlacementAxis::normalize(pixel, range) == expected,
                            "native normalization matches Qt");
                }
            }
        }
    }
    const QSize button(36, 36);
    constexpr int margin = 10;

    require(OverlayButtonPlacement::defaultPosition(landscape, button, margin) ==
                    QPoint(1054, 210),
            "default position must remain at the top-right corner");

    const QPoint dragged(777, 500);
    const QPointF normalized = OverlayButtonPlacement::normalize(
            dragged, landscape, button, margin);
    require(OverlayButtonPlacement::resolve(normalized, landscape, button, margin) == dragged,
            "normalized position must round-trip in the same viewport");

    const QRect portrait(-300, 40, 600, 1000);
    const QPoint portraitPosition = OverlayButtonPlacement::resolve(
            normalized, portrait, button, margin);
    require(portrait.contains(QRect(portraitPosition, button)),
            "restored position must remain inside a resized viewport");
    const QPointF portraitNormalized = OverlayButtonPlacement::normalize(
            portraitPosition, portrait, button, margin);
    require(std::abs(portraitNormalized.x() - normalized.x()) < 0.002 &&
                    std::abs(portraitNormalized.y() - normalized.y()) < 0.002,
            "resizing must preserve the relative position within pixel rounding");

    const QRect narrow(20, 30, 24, 18);
    require(OverlayButtonPlacement::clamp(QPoint(900, 900), narrow, button, margin) ==
                    narrow.topLeft(),
            "narrow viewports must not produce inverted bounds");

    QTemporaryDir temporaryDirectory;
    require(temporaryDirectory.isValid(), "temporary settings directory must be available");

    qputenv("MOONLIGHT_DEVICE_LOCAL_SETTINGS_DIR",
            temporaryDirectory.path().toLocal8Bit());
    const QString sharedLocalPath = DeviceLocalSettings::filePath(
            QStringLiteral("shared.ini"));
    require(QFileInfo(sharedLocalPath).absolutePath() ==
                    QDir(temporaryDirectory.path()).absolutePath(),
            "shared override must isolate device-local settings");

    const QString featureDirectory = temporaryDirectory.filePath(QStringLiteral("feature"));
    qputenv("MOONLIGHT_TEST_FEATURE_STATE_DIR", featureDirectory.toLocal8Bit());
    const QString featureLocalPath = DeviceLocalSettings::filePath(
            QStringLiteral("feature.ini"), "MOONLIGHT_TEST_FEATURE_STATE_DIR");
    require(QFileInfo(featureLocalPath).absolutePath() == QDir(featureDirectory).absolutePath(),
            "feature override must take precedence over the shared override");
    qunsetenv("MOONLIGHT_TEST_FEATURE_STATE_DIR");
    qunsetenv("MOONLIGHT_DEVICE_LOCAL_SETTINGS_DIR");

    const QString settingsPath = temporaryDirectory.filePath(QStringLiteral("position.ini"));

    OverlayButtonPositionStore store(settingsPath);
    require(!store.load().has_value(), "empty store must not return a custom position");
    require(store.save(normalized), "valid position must be saved");

    OverlayButtonPositionStore restoredStore(settingsPath);
    const auto restored = restoredStore.load();
    require(restored.has_value(), "saved position must be restored");
    require(nearlyEqual(restored->x(), normalized.x()) &&
                    nearlyEqual(restored->y(), normalized.y()),
            "stored normalized coordinates must be preserved");

    require(store.save(QPointF(-2.0, 4.0)),
            "out-of-range finite positions must be accepted and constrained");
    const auto constrained = store.load();
    require(constrained.has_value() && constrained->x() == 0.0 && constrained->y() == 1.0,
            "stored positions must be constrained to the normalized range");
    require(!store.save(QPointF(qInf(), 0.5)),
            "non-finite positions must be rejected");

    QSettings corruptSettings(settingsPath, QSettings::IniFormat);
    corruptSettings.setValue(QStringLiteral("meta/version"), 99);
    corruptSettings.sync();
    OverlayButtonPositionStore corruptStore(settingsPath);
    require(!corruptStore.load().has_value(),
            "unsupported position versions must safely fall back");

    return 0;
}
