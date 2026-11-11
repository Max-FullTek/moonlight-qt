#include "compatfetcher.h"
#include "nativeversion.h"
#include <QSettings>

#define COMPAT_VERSION "v1"
#define COMPAT_KEY "latestsupportedversion-"

CompatFetcher::CompatFetcher(QObject *parent) : QObject(parent) {}

bool CompatFetcher::isGfeVersionSupported(QString gfeVersion)
{
    QSettings settings;

    if (gfeVersion.trimmed().isEmpty()) {
        // If we don't have a GFE version, just allow it
        return true;
    }

    QString latestSupportedVersion = settings.value(COMPAT_KEY COMPAT_VERSION).toString();
    if (latestSupportedVersion.isEmpty()) {
        // We don't have compat data yet, so just assume it's supported
        return true;
    }

    const auto actual = NativeVersion::parseNumeric(gfeVersion.trimmed().toStdString());
    const auto latest = NativeVersion::parseNumeric(latestSupportedVersion.trimmed().toStdString());
    if (actual.size() < 2 || latest.size() < 2) {
        // Keep compatibility checks permissive when the server data is invalid.
        qWarning() << "Failed to parse GFE compatibility versions:" << gfeVersion
                   << latestSupportedVersion;
        return true;
    }
    if (NativeVersion::compare(actual, latest) > 0) {
        qWarning() << "GFE version" << gfeVersion
                   << "is not supported by this version of Moonlight";
        return false;
    }
    return true;
}
