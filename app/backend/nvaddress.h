#pragma once

#include <QString>
#ifndef LITE_GPUI_WORKER
#include <QHostAddress>
#endif

#define DEFAULT_HTTP_PORT 47989
#define DEFAULT_HTTPS_PORT 47984

class NvAddress
{
public:
    NvAddress();
    explicit NvAddress(QString addr, uint16_t port);
#ifndef LITE_GPUI_WORKER
    explicit NvAddress(QHostAddress addr, uint16_t port);
#endif

    uint16_t port() const;
    void setPort(uint16_t port);

    QString address() const;
    void setAddress(QString addr);
#ifndef LITE_GPUI_WORKER
    void setAddress(QHostAddress addr);
#endif

    bool isNull() const;
    QString toString() const;

    bool operator==(const NvAddress& other) const
    {
        return m_Address == other.m_Address &&
                m_Port == other.m_Port;
    }

    bool operator!=(const NvAddress& other) const
    {
        return !operator==(other);
    }

private:
    QString m_Address;
    uint16_t m_Port;
};
