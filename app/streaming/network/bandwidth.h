#pragma once

#include <atomic>
#include <cstdint>
#ifdef _WIN32
#include <chrono>
#else
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#endif

class BandwidthCalculator
#ifndef _WIN32
    : public QObject
#endif
{
#ifndef _WIN32
    Q_OBJECT
#endif

public:
    static BandwidthCalculator* instance();
    
    void addBytes(std::int64_t bytes);
    int getCurrentBandwidthKbps();
    
    void start();
    void stop();

private:
    friend class BandwidthCalculatorTest;
    BandwidthCalculator();
    ~BandwidthCalculator();
    
    static BandwidthCalculator *s_instance;
    
    std::atomic<std::int64_t> m_bytesReceived;
    std::int64_t m_lastBytesReceived;
    std::atomic<int> m_currentBandwidthKbps;
    bool m_running;
    
#ifdef _WIN32
    std::uintptr_t m_updateTimer = 0;
    std::chrono::steady_clock::time_point m_lastUpdate;
#else
    QTimer m_updateTimer;
    QElapsedTimer m_elapsedTimer;
#endif
    
#ifndef _WIN32
private slots:
#endif
    void updateBandwidth();
};
