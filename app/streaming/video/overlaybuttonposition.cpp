#include "overlaybuttonposition.h"
#include "overlayplacementaxis.h"

#include "settings/devicelocalsettings.h"

#include <QtGlobal>

#ifdef LITE_GPUI_WORKER
#include "backend/legacyworkerprofile.h"
#include "backend/windowsprofilefile.h"
#include <charconv>
#endif

namespace {
constexpr auto LocalStateFileName = "overlay-button-placement.ini";
constexpr auto LocalStateVersionKey = "meta/version";
constexpr auto NormalizedXKey = "position/normalizedX";
constexpr auto NormalizedYKey = "position/normalizedY";
constexpr int LocalStateVersion = 1;

#ifdef LITE_GPUI_WORKER
constexpr size_t MaxPlacementBytes = 64 * 1024;

std::optional<double> readCoordinate(const LegacyWorkerProfile::Values& values, const char* key)
{
    auto text = LegacyWorkerProfile::value(values, key);
    if (text.empty()) return std::nullopt;
    if (text.front() == '+') text.remove_prefix(1);
    double coordinate;
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), coordinate);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() ||
            !qIsFinite(coordinate)) return std::nullopt;
    return qBound(0.0, coordinate, 1.0);
}
#endif

QString placementPath(const QString& customPath)
{
    return customPath.isEmpty()
        ? DeviceLocalSettings::filePath(QLatin1String(LocalStateFileName),
                                        "MOONLIGHT_OVERLAY_BUTTON_PLACEMENT_DIR")
        : customPath;
}
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
#ifdef LITE_GPUI_WORKER
    : m_LegacyPath(placementPath(settingsPath).toStdWString()),
      m_Path(m_LegacyPath.parent_path() / (m_LegacyPath.stem().wstring() + L"-v1.ini"))
#else
    : m_Settings(placementPath(settingsPath), QSettings::IniFormat)
#endif
{
}

std::optional<QPointF> OverlayButtonPositionStore::load() const
{
#ifdef LITE_GPUI_WORKER
    try {
        auto data = WindowsProfileFile::read(m_Path, MaxPlacementBytes);
        if (!data) data = WindowsProfileFile::read(m_LegacyPath, MaxPlacementBytes);
        if (!data) return std::nullopt;
        const auto values = LegacyWorkerProfile::parseIni(*data);
        if (LegacyWorkerProfile::number(values, LocalStateVersionKey) != LocalStateVersion) {
            return std::nullopt;
        }
        const auto x = readCoordinate(values, NormalizedXKey);
        const auto y = readCoordinate(values, NormalizedYKey);
        if (x && y) return QPointF(*x, *y);
    } catch (const std::exception&) {
        qWarning("Unable to read overlay button placement");
    }
    return std::nullopt;
#else
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
#endif
}

bool OverlayButtonPositionStore::save(const QPointF& normalizedPosition)
{
    if (!qIsFinite(normalizedPosition.x()) || !qIsFinite(normalizedPosition.y())) {
        return false;
    }

#ifdef LITE_GPUI_WORKER
    const auto x = QString::number(qBound<qreal>(0.0, normalizedPosition.x(), 1.0), 'g', 17);
    const auto y = QString::number(qBound<qreal>(0.0, normalizedPosition.y(), 1.0), 'g', 17);
    const auto data = QStringLiteral("[meta]\nversion=%1\n\n[position]\nnormalizedX=%2\nnormalizedY=%3\n")
        .arg(QString::number(LocalStateVersion), x, y);
    try {
        WindowsProfileFile::replace(m_Path, data.toStdString());
        return true;
    } catch (const std::exception&) {
        return false;
    }
#else
    m_Settings.setValue(QLatin1String(LocalStateVersionKey), LocalStateVersion);
    m_Settings.setValue(QLatin1String(NormalizedXKey),
                        qBound<qreal>(0.0, normalizedPosition.x(), 1.0));
    m_Settings.setValue(QLatin1String(NormalizedYKey),
                        qBound<qreal>(0.0, normalizedPosition.y(), 1.0));
    m_Settings.sync();
    return m_Settings.status() == QSettings::NoError;
#endif
}
