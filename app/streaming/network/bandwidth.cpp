#include "bandwidth.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

#ifdef Q_OS_DARWIN
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#ifdef Q_OS_LINUX
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#endif

BandwidthCalculator *BandwidthCalculator::s_instance = nullptr;

BandwidthCalculator *BandwidthCalculator::instance()
{
    if (s_instance == nullptr)
    {
        s_instance = new BandwidthCalculator();
    }
    return s_instance;
}

class SystemNetworkStats
{
public:
    static bool getNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent);

private:
#ifdef _WIN32
    static bool getWindowsNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent);
#elif defined(Q_OS_LINUX)
    static bool getLinuxNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent);
#elif defined(Q_OS_DARWIN)
    static bool getMacOSNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent);
#endif
};

BandwidthCalculator::BandwidthCalculator() : m_bytesReceived(0),
                                             m_lastBytesReceived(0),
                                             m_currentBandwidthKbps(0),
                                             m_running(false)
{
#ifndef _WIN32
    connect(&m_updateTimer, &QTimer::timeout,
            this, &BandwidthCalculator::updateBandwidth);
#endif
}

BandwidthCalculator::~BandwidthCalculator()
{
    stop();
}

void BandwidthCalculator::addBytes(std::int64_t bytes)
{
    if (m_running)
    {
        m_bytesReceived += bytes;
    }
}

int BandwidthCalculator::getCurrentBandwidthKbps()
{
    return m_currentBandwidthKbps.load();
}

void BandwidthCalculator::start()
{
    if (!m_running)
    {
        m_bytesReceived = 0;
        m_lastBytesReceived = 0;
        m_currentBandwidthKbps = 0;
#ifdef _WIN32
        m_lastUpdate = std::chrono::steady_clock::now();
        m_updateTimer = SetTimer(nullptr, 0, 1000,
            [](HWND, UINT, UINT_PTR id, DWORD) {
                // KillTimer does not remove a queued WM_TIMER. Ignore stale
                // callbacks after stop, and keep all sampling on the owner.
                auto* self = s_instance;
                if (self && self->m_running && self->m_updateTimer == id) {
                    self->updateBandwidth();
                }
            });
        if (!m_updateTimer) return;
#else
        m_elapsedTimer.start();
        m_updateTimer.start(1000); // 每秒更新一次
#endif
        m_running = true;
    }
}

void BandwidthCalculator::stop()
{
    if (m_running)
    {
#ifdef _WIN32
        KillTimer(nullptr, m_updateTimer);
        m_updateTimer = 0;
#else
        m_updateTimer.stop();
#endif
        m_running = false;
    }
}

void BandwidthCalculator::updateBandwidth()
{
    std::int64_t currentBytes = 0;
    std::int64_t sentBytes = 0;

    // 从系统获取实际网络使用情况
    if (SystemNetworkStats::getNetworkUsage(currentBytes, sentBytes))
    {
        m_bytesReceived = currentBytes;
    }
    else
    {
        // 如果获取失败，则使用手动计算的值
        currentBytes = m_bytesReceived.load();
    }

    std::int64_t bytesTransferred = currentBytes - m_lastBytesReceived;

    // 计算带宽（bits per second）
#ifdef _WIN32
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastUpdate).count();
    m_lastUpdate = now;
#else
    const auto elapsedMs = m_elapsedTimer.restart();
#endif
    if (elapsedMs > 0)
    {
        // 转换为Kbps (bits per second / 1000)
        m_currentBandwidthKbps = static_cast<int>((bytesTransferred * 8.0 * 1000) / elapsedMs / 1000);
    }

    m_lastBytesReceived = currentBytes;
}

// 系统网络统计实现
bool SystemNetworkStats::getNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent)
{
#ifdef _WIN32
    return getWindowsNetworkUsage(bytesReceived, bytesSent);
#elif defined(Q_OS_LINUX)
    return getLinuxNetworkUsage(bytesReceived, bytesSent);
#elif defined(Q_OS_DARWIN)
    return getMacOSNetworkUsage(bytesReceived, bytesSent);
#else
    Q_UNUSED(bytesReceived);
    Q_UNUSED(bytesSent);
    return false;
#endif
}

#ifdef _WIN32
bool SystemNetworkStats::getWindowsNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent)
{
    // 初始化计数器
    bytesReceived = 0;
    bytesSent = 0;

    PMIB_IF_TABLE2 pIfTable = NULL;
    ULONG error = GetIfTable2(&pIfTable);

    if (error != NO_ERROR)
    {
        return false;
    }

    // 累加有效网络接口的流量
    for (ULONG i = 0; i < pIfTable->NumEntries; i++)
    {
        MIB_IF_ROW2 row = pIfTable->Table[i];

        // 确保只计算有效的物理网络接口
        // 排除虚拟适配器、隧道接口等
        if (row.OperStatus == IfOperStatusUp &&
            row.MediaType != NdisMediumLoopback &&
            row.Type != IF_TYPE_SOFTWARE_LOOPBACK &&
            row.Type != IF_TYPE_TUNNEL &&
            !(row.InterfaceAndOperStatusFlags.FilterInterface) &&
            row.TransmitLinkSpeed > 0 && 
            row.ReceiveLinkSpeed > 0)
        {
            // 检查是否是实际在用的接口 - 有流量的接口
            if (row.InOctets > 0 || row.OutOctets > 0) {
                bytesReceived += row.InOctets;
                bytesSent += row.OutOctets;
            }
        }
    }

    // 释放表资源
    FreeMibTable(pIfTable);

    return true;
}
#elif defined(Q_OS_LINUX)
bool SystemNetworkStats::getLinuxNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent)
{
    // Linux 实现：读取 /proc/net/dev 文件
    QFile file("/proc/net/dev");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    bytesReceived = 0;
    bytesSent = 0;

    QTextStream in(&file);
    QString line = in.readLine(); // 跳过前两行标题
    line = in.readLine();

    while (!in.atEnd())
    {
        line = in.readLine();
        QStringList parts = line.trimmed().split(QRegularExpression("\\s+"));
        if (parts.size() >= 10)
        {
            // 每个网络接口行格式：Interface: rx_bytes ... tx_bytes ...
            QString interface = parts[0].remove(":");
            if (interface != "lo")
            { // 忽略本地回环接口
                bytesReceived += parts[1].toLongLong();
                bytesSent += parts[9].toLongLong();
            }
        }
    }

    file.close();
    return true;
}
#elif defined(Q_OS_DARWIN)
bool SystemNetworkStats::getMacOSNetworkUsage(std::int64_t &bytesReceived, std::int64_t &bytesSent)
{
    struct ifaddrs *ifaddrs;
    bytesReceived = 0;
    bytesSent = 0;

    if (getifaddrs(&ifaddrs) == -1) {
        return false;
    }

    for (struct ifaddrs *ifa = ifaddrs; ifa != nullptr; ifa = ifa->ifa_next) {
        // 忽略非网络接口
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_LINK) {
            continue;
        }

        // 忽略回环接口
        if (strncmp(ifa->ifa_name, "lo", 2) == 0) {
            continue;
        }

        // 获取接口数据
        if (ifa->ifa_data != nullptr) {
            struct if_data *stats = (struct if_data *)ifa->ifa_data;
            bytesReceived += stats->ifi_ibytes;
            bytesSent += stats->ifi_obytes;
        }
    }

    freeifaddrs(ifaddrs);
    return true;
}
#endif
