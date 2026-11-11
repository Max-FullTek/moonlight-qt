#pragma once

#include <QString>

#ifdef _WIN32
#include "windowsclienthostname.h"
#else
#include <QHostInfo>
#endif

inline QString localClientHostname()
{
#ifdef _WIN32
    return QString::fromStdWString(windowsClientHostname());
#else
    return QHostInfo::localHostName();
#endif
}
