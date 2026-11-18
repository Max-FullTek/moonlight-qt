#pragma once

#include <QMetaType>
#include <QString>

#ifndef LITE_GPUI_WORKER
class QSettings;
#endif

class NvApp
{
public:
    NvApp() {}
#ifndef LITE_GPUI_WORKER
    explicit NvApp(QSettings& settings);
#endif

    bool operator==(const NvApp& other) const
    {
        return id == other.id &&
                name == other.name &&
                hdrSupported == other.hdrSupported &&
                isAppCollectorGame == other.isAppCollectorGame &&
                hidden == other.hidden &&
                directLaunch == other.directLaunch;
    }

    bool operator!=(const NvApp& other) const
    {
        return !operator==(other);
    }

    bool isInitialized()
    {
        // We use isNull() instead of isEmpty() here because we want
        // to detect cases where the name is unassigned, not empty.
        return id != 0 && !name.isNull();
    }

#ifndef LITE_GPUI_WORKER
    void
    serialize(QSettings& settings) const;
#endif

    int id = 0;
    QString name;
    bool hdrSupported = false;
    bool isAppCollectorGame = false;
    bool hidden = false;
    bool directLaunch = false;
};

Q_DECLARE_METATYPE(NvApp)
