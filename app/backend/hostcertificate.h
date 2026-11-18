#pragma once

#ifdef LITE_GPUI_WORKER
#include <QByteArray>

// The paired leaf certificate. Identity comparisons always use the complete DER.
class HostCertificate
{
public:
    HostCertificate() = default;
    explicit HostCertificate(const QByteArray& pem);

    bool isNull() const { return m_Der.isEmpty(); }
    const QByteArray& toPem() const { return m_Pem; }
    const QByteArray& toDer() const { return m_Der; }

    bool operator==(const HostCertificate& other) const { return m_Der == other.m_Der; }
    bool operator!=(const HostCertificate& other) const { return !(*this == other); }

private:
    QByteArray m_Pem;
    QByteArray m_Der;
};
#else
#include <QSslCertificate>
using HostCertificate = QSslCertificate;
#endif
