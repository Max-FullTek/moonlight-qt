#include "overlaybuttonposition.h"
#include "overlayplacementaxis.h"

#include "settings/devicelocalsettings.h"

#include <QtGlobal>

namespace {
constexpr auto LocalStateFileName = "overlay-button-placement.ini";
constexpr auto LocalStateVersionKey = "meta/version";
constexpr auto NormalizedXKey = "position/normalizedX";
constexpr auto NormalizedYKey = "position/normalizedY";
constexpr int LocalStateVersion = 1;

}

QPoint OverlayButtonPlacement::defaultPosition(const QRect& parentGeometry,
                                               const QSize& buttonSize,
                                               int margin)
{
    return resolve(QPointF(1.0, 0.0), parentGeometry, buttonSize, margin);
}

QPoint OverlayButtonPlacement::clamp(const QPoint& position,
                                     const QRect& parentGeometry,
                                     const QSize& buttonSize,
                                     int margin)
{
    if (!parentGeometry.isValid() || !buttonSize.isValid()) {
        return position;
    }

    const OverlayPlacementAxis::Bounds horizontal = OverlayPlacementAxis::bounds(parentGeometry.x(),
                                             parentGeometry.width(),
                                             buttonSize.width(),
                                             margin);
    const OverlayPlacementAxis::Bounds vertical = OverlayPlacementAxis::bounds(parentGeometry.y(),
                                           parentGeometry.height(),
                                           buttonSize.height(),
                                           margin);
    return QPoint(OverlayPlacementAxis::clamp(position.x(), horizontal),
                  OverlayPlacementAxis::clamp(position.y(), vertical));
}

QPoint OverlayButtonPlacement::resolve(const QPointF& normalizedPosition,
                                       const QRect& parentGeometry,
                                       const QSize& buttonSize,
                                       int margin)
{
    if (!parentGeometry.isValid() || !buttonSize.isValid()) {
        return parentGeometry.topLeft();
    }

    const OverlayPlacementAxis::Bounds horizontal = OverlayPlacementAxis::bounds(parentGeometry.x(),
                                             parentGeometry.width(),
                                             buttonSize.width(),
                                             margin);
    const OverlayPlacementAxis::Bounds vertical = OverlayPlacementAxis::bounds(parentGeometry.y(),
                                           parentGeometry.height(),
                                           buttonSize.height(),
                                           margin);
    return QPoint(OverlayPlacementAxis::resolve(normalizedPosition.x(), horizontal),
                  OverlayPlacementAxis::resolve(normalizedPosition.y(), vertical));
}

QPointF OverlayButtonPlacement::normalize(const QPoint& position,
                                          const QRect& parentGeometry,
                                          const QSize& buttonSize,
                                          int margin)
{
    if (!parentGeometry.isValid() || !buttonSize.isValid()) {
        return QPointF(0.5, 0.5);
    }

    const OverlayPlacementAxis::Bounds horizontal = OverlayPlacementAxis::bounds(parentGeometry.x(),
                                             parentGeometry.width(),
                                             buttonSize.width(),
                                             margin);
    const OverlayPlacementAxis::Bounds vertical = OverlayPlacementAxis::bounds(parentGeometry.y(),
                                           parentGeometry.height(),
                                           buttonSize.height(),
                                           margin);
    const QPoint constrained = clamp(position, parentGeometry, buttonSize, margin);
    return QPointF(OverlayPlacementAxis::normalize(constrained.x(), horizontal),
                   OverlayPlacementAxis::normalize(constrained.y(), vertical));
}

OverlayButtonPositionStore::OverlayButtonPositionStore(const QString& settingsPath)
    : m_Settings(settingsPath.isEmpty()
                         ? DeviceLocalSettings::filePath(
                                   QLatin1String(LocalStateFileName),
                                   "MOONLIGHT_OVERLAY_BUTTON_PLACEMENT_DIR")
                         : settingsPath,
                 QSettings::IniFormat)
{
}

std::optional<QPointF> OverlayButtonPositionStore::load() const
{
    if (m_Settings.value(QLatin1String(LocalStateVersionKey), 0).toInt() !=
            LocalStateVersion) {
        return std::nullopt;
    }

    bool xValid = false;
    bool yValid = false;
    const qreal x = m_Settings.value(QLatin1String(NormalizedXKey)).toDouble(&xValid);
    const qreal y = m_Settings.value(QLatin1String(NormalizedYKey)).toDouble(&yValid);
    if (!xValid || !yValid || !qIsFinite(x) || !qIsFinite(y)) {
        return std::nullopt;
    }

    return QPointF(qBound<qreal>(0.0, x, 1.0),
                   qBound<qreal>(0.0, y, 1.0));
}

bool OverlayButtonPositionStore::save(const QPointF& normalizedPosition)
{
    if (!qIsFinite(normalizedPosition.x()) || !qIsFinite(normalizedPosition.y())) {
        return false;
    }

    m_Settings.setValue(QLatin1String(LocalStateVersionKey), LocalStateVersion);
    m_Settings.setValue(QLatin1String(NormalizedXKey),
                        qBound<qreal>(0.0, normalizedPosition.x(), 1.0));
    m_Settings.setValue(QLatin1String(NormalizedYKey),
                        qBound<qreal>(0.0, normalizedPosition.y(), 1.0));
    m_Settings.sync();
    return m_Settings.status() == QSettings::NoError;
}
