#pragma once

#ifndef LITE_GPUI_WORKER
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#endif
#include <QByteArray>
#include <QString>

class IdentityManager
{
public:
    QString
    getUniqueId();

    QByteArray
    getCertificate();

    QByteArray
    getPrivateKey();

#ifndef LITE_GPUI_WORKER
    QSslConfiguration
    getSslConfig();
#endif

    static
    IdentityManager*
    get();

private:
    IdentityManager();

#ifndef LITE_GPUI_WORKER
    QSslCertificate
    getSslCertificate();

    QSslKey
    getSslKey();
#endif

    bool hasReadableCertificate();
    bool hasReadablePrivateKey();

    void
    createCredentials();

    // Initialized in constructor
    QByteArray m_CachedPrivateKey;
    QByteArray m_CachedPemCert;

    // Lazy initialized
    QString m_CachedUniqueId;
#ifndef LITE_GPUI_WORKER
    QSslCertificate m_CachedSslCert;
    QSslKey m_CachedSslKey;
#endif

    static IdentityManager* s_Im;
};
