#include "identitymanager.h"
#include "nativeidentity.h"

#include <QDebug>
#ifdef LITE_NATIVE_WORKER_JSON
#include "workerpreferences.h"
#include "windowsprofilefile.h"
#include <nlohmann/json.hpp>
#else
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#endif


#define SER_UNIQUEID "uniqueid"
#define SER_CERT "certificate"
#define SER_KEY "key"

IdentityManager* IdentityManager::s_Im = nullptr;

IdentityManager*
IdentityManager::get()
{
    // This will always be called first on the main thread,
    // so it's safe to initialize without locks.
    if (s_Im == nullptr) {
        s_Im = new IdentityManager();
    }

    return s_Im;
}

void IdentityManager::createCredentials()
{
    const auto credentials = NativeIdentity::generateCredentials();
    // A valid old certificate may already be cached when its key is rejected.
    // Both wrappers must refer to the newly generated identity together.
    m_CachedSslCert = QSslCertificate();
    m_CachedSslKey = QSslKey();
    m_CachedPemCert = QByteArray::fromStdString(credentials.certificatePem);
    m_CachedPrivateKey = QByteArray::fromStdString(credentials.privateKeyPem);

    // Check that the new keypair is valid before persisting it
    if (getSslCertificate().isNull()) {
        qFatal("Newly generated certificate is unreadable");
    }
    if (getSslKey().isNull()) {
        qFatal("Newly generated private key is unreadable");
    }

}

IdentityManager::IdentityManager()
{
#ifdef LITE_NATIVE_WORKER_JSON
    if (!WorkerPreferences::directory().empty()) {
        try {
            const auto& directory = WorkerPreferences::directory();
            const auto path = directory / L"identity-v1.json";
            if (const auto bytes = WindowsProfileFile::read(path, 64 * 1024)) {
                const auto record = nlohmann::json::parse(*bytes);
                const auto& version = record.at("version");
                m_CachedUniqueId = QString::fromStdString(record.at("unique_id").get<std::string>());
                m_CachedPemCert = QByteArray::fromStdString(record.at("certificate").get<std::string>());
                m_CachedPrivateKey = QByteArray::fromStdString(record.at("private_key").get<std::string>());
                if (!version.is_number_integer() || version != 1 || m_CachedUniqueId.isEmpty() ||
                    getSslCertificate().isNull() || getSslKey().isNull()) {
                    qFatal("Invalid GPUI identity");
                }
                return;
            }

            const auto& legacy = LegacyWorkerProfile::read(directory);
            const auto bytes = [&](const char* key) {
                const auto value = LegacyWorkerProfile::value(legacy, key);
                return QByteArray(value.data(), static_cast<qsizetype>(value.size()));
            };
            m_CachedPemCert = bytes(SER_CERT);
            m_CachedPrivateKey = bytes(SER_KEY);
            if (getSslCertificate().isNull() || getSslKey().isNull()) createCredentials();
            m_CachedUniqueId = QString::fromUtf8(LegacyWorkerProfile::value(legacy, SER_UNIQUEID));
            if (m_CachedUniqueId.isEmpty()) {
                m_CachedUniqueId = QString::fromStdString(NativeIdentity::generateUniqueId());
            }
            // The Rust frontend installs the identity received over the private
            // pipe. Direct worker callers still need persistence across runs.
            if (!qEnvironmentVariableIsSet("MOONLIGHT_GPUI_RUST_IDENTITY")) {
                const nlohmann::json record{{"version", 1}, {"unique_id", m_CachedUniqueId.toStdString()},
                    {"certificate", m_CachedPemCert.toStdString()}, {"private_key", m_CachedPrivateKey.toStdString()}};
                WindowsProfileFile::replace(path, record.dump());
            }
            return;
        } catch (const std::exception&) {
            qFatal("Unable to initialize the GPUI identity");
        }
    }
#else
    if (qEnvironmentVariableIsSet("MOONLIGHT_GPUI_RUST_IDENTITY")) {
        const auto profile = qEnvironmentVariable("MOONLIGHT_GPUI_PROFILE");
        if (!profile.isEmpty()) {
            QFile identity(QDir(profile).filePath("identity-v1.json"));
            if (identity.exists()) {
                if (!identity.open(QIODevice::ReadOnly) || identity.size() > 64 * 1024) {
                    qFatal("Unable to read the GPUI identity");
                }
                const auto record = QJsonDocument::fromJson(identity.readAll()).object();
                m_CachedUniqueId = record.value("unique_id").toString();
                m_CachedPemCert = record.value("certificate").toString().toLatin1();
                m_CachedPrivateKey = record.value("private_key").toString().toLatin1();
                if (record.value("version").toInt() != 1 || m_CachedUniqueId.isEmpty() ||
                    getSslCertificate().isNull() || getSslKey().isNull()) {
                    qFatal("Invalid GPUI identity");
                }
                return;
            }
        }
    }
#endif
    QSettings settings;

    m_CachedPemCert = settings.value(SER_CERT).toByteArray();
    m_CachedPrivateKey = settings.value(SER_KEY).toByteArray();

    if (m_CachedPemCert.isEmpty() || m_CachedPrivateKey.isEmpty()) {
        qInfo() << "No existing credentials found";
        createCredentials();
        settings.setValue(SER_CERT, m_CachedPemCert);
        settings.setValue(SER_KEY, m_CachedPrivateKey);
    }
    else if (getSslCertificate().isNull()) {
        qWarning() << "Certificate is unreadable";
        createCredentials();
        settings.setValue(SER_CERT, m_CachedPemCert);
        settings.setValue(SER_KEY, m_CachedPrivateKey);
    }
    else if (getSslKey().isNull()) {
        qWarning() << "Private key is unreadable";
        createCredentials();
        settings.setValue(SER_CERT, m_CachedPemCert);
        settings.setValue(SER_KEY, m_CachedPrivateKey);
    }

    // We should have valid credentials now. If not, we're screwed
    if (getSslCertificate().isNull()) {
        qFatal("Certificate is unreadable");
    }
    if (getSslKey().isNull()) {
        qFatal("Private key is unreadable");
    }

    // Load the unique ID from settings
    m_CachedUniqueId = settings.value(SER_UNIQUEID).toString();
    if (!m_CachedUniqueId.isEmpty()) {
        qInfo() << "Loaded unique ID from settings:" << m_CachedUniqueId;
    }
    else {
        // Generate a new unique ID in base 16
        m_CachedUniqueId = QString::fromStdString(NativeIdentity::generateUniqueId());

        qInfo() << "Generated new unique ID:" << m_CachedUniqueId;

        settings.setValue(SER_UNIQUEID, m_CachedUniqueId);
    }
}

QSslCertificate
IdentityManager::getSslCertificate()
{
    if (m_CachedSslCert.isNull()) {
        m_CachedSslCert = QSslCertificate(m_CachedPemCert);
    }
    return m_CachedSslCert;
}

QSslKey
IdentityManager::getSslKey()
{
    if (m_CachedSslKey.isNull()) {
        // Keep the existing SecureTransport PKCS1 boundary on Darwin.
#ifdef Q_OS_DARWIN
        const bool traditional = true;
#else
        const bool traditional = false;
#endif
        const auto normalized = NativeIdentity::normalizePrivateKey(m_CachedPrivateKey.toStdString(), traditional);
        m_CachedSslKey = QSslKey(QByteArray::fromStdString(normalized), QSsl::Rsa);
    }
    return m_CachedSslKey;
}

QSslConfiguration
IdentityManager::getSslConfig()
{
    QSslConfiguration sslConfig(QSslConfiguration::defaultConfiguration());
    sslConfig.setLocalCertificate(getSslCertificate());
    sslConfig.setPrivateKey(getSslKey());
    return sslConfig;
}

QString
IdentityManager::getUniqueId()
{
    return m_CachedUniqueId;
}

QByteArray
IdentityManager::getCertificate()
{
    return m_CachedPemCert;
}

QByteArray
IdentityManager::getPrivateKey()
{
    return m_CachedPrivateKey;
}
