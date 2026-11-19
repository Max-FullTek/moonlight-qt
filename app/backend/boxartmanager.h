#pragma once

#include "computermanager.h"
#include <QDir>
#ifndef LITE_GPUI_WORKER
#include <QImage>
#include <QHash>
#include <QMutex>
#include <QThreadPool>
#include <QRunnable>
#endif

class BoxArtManager
#ifndef LITE_GPUI_WORKER
    : public QObject
#endif
{
public:
    static void deleteBoxArt(NvComputer* computer);

#ifndef LITE_GPUI_WORKER
    Q_OBJECT

    friend class NetworkBoxArtLoadTask;

public:
    explicit BoxArtManager(QObject *parent = nullptr);

    QUrl
    loadBoxArt(NvComputer* computer, NvApp& app);

signals:
    void
    boxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image);

public slots:

private slots:
    void
    handleBoxArtLoadComplete(NvComputer* computer, NvApp app, QUrl image);

private:
    QUrl
    loadBoxArtFromNetwork(NvComputer* computer, const NvApp& app);

    static bool
    isPlaceholderBoxArt(const QSize& size);

    bool
    isCachedPlaceholderBoxArt(const QString& cachePath);

    void
    rememberPlaceholderBoxArt(const QString& cachePath, bool isPlaceholder);

    QString
    getFilePathForBoxArt(NvComputer* computer, int appId);

    QDir m_BoxArtDir;
    QThreadPool m_ThreadPool;
    QMutex m_PlaceholderCacheMutex;
    QHash<QString, bool> m_PlaceholderCache;
#endif
};
