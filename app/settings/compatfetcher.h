#pragma once

#include <QObject>

class CompatFetcher : public QObject
{
    Q_OBJECT

public:
    explicit CompatFetcher(QObject *parent = nullptr);

    static bool isGfeVersionSupported(QString gfeVersion);

};
