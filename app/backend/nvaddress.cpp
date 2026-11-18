#include "nvaddress.h"

#ifdef LITE_GPUI_WORKER
#include "windowsreachability.h"
#endif

NvAddress::NvAddress()
{
    setAddress(nullptr);
    setPort(0);
}

NvAddress::NvAddress(QString addr, uint16_t port)
{
    setAddress(addr);
    setPort(port);
}

#ifndef LITE_GPUI_WORKER
NvAddress::NvAddress(QHostAddress addr, uint16_t port)
{
    setAddress(addr);
    setPort(port);
}
#endif

uint16_t NvAddress::port() const
{
    return m_Port;
}

QString NvAddress::address() const
{
    return m_Address;
}

void NvAddress::setPort(uint16_t port)
{
    m_Port = port;
}

void NvAddress::setAddress(QString addr)
{
    m_Address = addr;
}

#ifndef LITE_GPUI_WORKER
void NvAddress::setAddress(QHostAddress addr)
{
    m_Address = addr.toString();
}
#endif

bool NvAddress::isNull() const
{
    return m_Address.isEmpty();
}

QString NvAddress::toString() const
{
    if (m_Address.isEmpty()) {
        return "<NULL>";
    }

#ifdef LITE_GPUI_WORKER
    const bool ipv6 = WindowsReachability::parseLiteral(m_Address.trimmed().toStdWString()).size == 16;
#else
    const bool ipv6 = QHostAddress(m_Address).protocol() == QAbstractSocket::IPv6Protocol;
#endif
    if (ipv6) {
        return QString("[%1]:%2").arg(m_Address).arg(m_Port);
    }
    else {
        return QString("%1:%2").arg(m_Address).arg(m_Port);
    }
}
