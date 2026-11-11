#include "backend/pairedcertificate.h"
#include "nativeversion.h"
#include "settings/compatfetcher.h"
#include "versionutils.h"

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSettings>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTextStream>

#ifdef Q_OS_WIN
#include "backend/windowsreachability.h"
#endif

namespace {
bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << '\n';
    }
    return condition;
}

QSslCertificate certificate(const char* name)
{
    QFile file(QStringLiteral(":/clipboard-test/") + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? QSslCertificate(file.readAll()) : QSslCertificate();
}
} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;

    const QHostAddress ipv4Local(QStringLiteral("192.168.1.1"));
    const QHostAddress ipv4Peer(QStringLiteral("192.168.1.129"));
    const QHostAddress ipv6Local(QStringLiteral("2001:db8::1"));
    const QHostAddress ipv6Peer(QStringLiteral("2001:db8:0:0:8000::1"));
    ok &= require(ipv4Local.isInSubnet(ipv4Peer, 0) &&
                      ipv4Local.isInSubnet(ipv4Peer, 24) &&
                      !ipv4Local.isInSubnet(ipv4Peer, 25) &&
                      !ipv4Local.isInSubnet(ipv4Peer, 33) &&
                      ipv6Local.isInSubnet(ipv6Peer, 64) &&
                      !ipv6Local.isInSubnet(ipv6Peer, 65) &&
                      ipv6Local.isInSubnet(ipv6Local, 129) &&
                      !ipv4Local.isInSubnet(QHostAddress(QStringLiteral("::ffff:192.168.1.1")), 0),
                  QStringLiteral("Qt subnet behavior matches the native reachability cases"), err);

#ifdef Q_OS_WIN
    const auto addresses = QNetworkInterface::allInterfaces();
    QList<QHostAddress> checked;
    for (const auto& nic : addresses) {
        if (!(nic.flags() & QNetworkInterface::IsUp)) {
            continue;
        }
        const auto entries = nic.addressEntries();
        for (const auto& entry : entries) {
            const auto ip = entry.ip();
            if (checked.contains(ip)) {
                continue;
            }
            WindowsReachability::Address local;
            if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                const auto bytes = ReachabilityPolicy::ipv4Bytes(ip.toIPv4Address());
                std::copy(bytes.begin(), bytes.end(), local.bytes.begin());
                local.size = 4;
            } else if (ip.protocol() == QAbstractSocket::IPv6Protocol) {
                const auto bytes = ip.toIPv6Address();
                std::copy_n(bytes.c, 16, local.bytes.begin());
                local.size = 16;
            } else {
                continue;
            }
            checked.append(ip);
            using ReachabilityPolicy::Route;
            const bool vpn = nic.type() == QNetworkInterface::Virtual ||
                nic.type() == QNetworkInterface::Ppp ||
                (nic.maximumTransmissionUnit() != 0 && nic.maximumTransmissionUnit() < 1500) ||
                (nic.flags() & QNetworkInterface::IsPointToPoint) ||
                nic.name().startsWith("iftype53_") || nic.name().startsWith("iftype131_") ||
                nic.hardwareAddress().startsWith("00:FF", Qt::CaseInsensitive) ||
                nic.humanReadableName().startsWith("ZeroTier") ||
                nic.humanReadableName().contains("VPN");
            const auto expected = vpn ? Route::Vpn : entry.prefixLength() >= 0 ? Route::Lan : Route::Unknown;
            ok &= require(WindowsReachability::classify(local, local) == expected,
                          QStringLiteral("Windows adapter classification differs for %1").arg(ip.toString()), err);
        }
    }
    ok &= require(!checked.isEmpty(), QStringLiteral("at least one Windows adapter was compared"), err);
    QTextStream(stdout) << "windows_adapter_addresses_compared=" << checked.size() << '\n';
    for (const auto& ip : {QHostAddress(QHostAddress::LocalHost), QHostAddress(QHostAddress::LocalHostIPv6)}) {
        QTcpServer listener;
        ok &= require(listener.listen(ip), QStringLiteral("loopback listener starts for %1").arg(ip.toString()), err);
        if (!listener.isListening()) {
            continue;
        }
        const auto host = ip.toString().toStdWString();
        const auto port = listener.serverPort();
        const auto connected = WindowsReachability::probeNumeric(host.c_str(), port);
        ok &= require(connected && *connected != ReachabilityPolicy::Route::Unknown &&
                          listener.waitForNewConnection(1000),
                      QStringLiteral("Winsock probes the actual loopback listener at %1").arg(ip.toString()), err);
        listener.close();
        ok &= require(WindowsReachability::probeNumeric(host.c_str(), port) == ReachabilityPolicy::Route::Unknown,
                      QStringLiteral("closed loopback port falls back to unknown route"), err);
    }
    ok &= require(!WindowsReachability::probeNumeric(L"localhost", 1),
                  QStringLiteral("numeric probe leaves hostnames to the existing resolver"), err);
#endif

    ok &= require(VersionUtils::parseRelease("v6.2.82+14.g13ca12da.dirty") ==
                      QVersionNumber(6, 2, 82),
                  QStringLiteral("Git build metadata must not alter the release version"), err);
    ok &= require(VersionUtils::parseRelease(" V6.2.82-14-g13ca12da ") == QVersionNumber(6, 2, 82),
                  QStringLiteral("legacy Git suffix and uppercase prefix are accepted"), err);
    ok &= require(VersionUtils::compare(QVersionNumber(1, 2), QVersionNumber(1, 2, 0)) == 0,
                  QStringLiteral("missing trailing segments must compare as zero"), err);
    ok &= require(VersionUtils::compare(QVersionNumber(6, 9), QVersionNumber(6, 10)) < 0,
                  QStringLiteral("version components compare numerically"), err);
    for (const char* invalid : { "", "garbage", "1.2garbage", "1..2", "-1.2", "1.2.2147483648" }) {
        ok &= require(VersionUtils::parseNumeric(QString::fromLatin1(invalid)).isNull(),
                      QStringLiteral("invalid numeric version must not be partially accepted: %1")
                          .arg(invalid),
                      err);
    }
    for (const QString& input : {QStringLiteral(" 1.02.0 "), QStringLiteral("1.2147483647"),
                                QStringLiteral("1.2147483648"), QStringLiteral("1."),
                                QStringLiteral("+1.2"), QStringLiteral("1.+2"),
                                QStringLiteral("1. 2"), QStringLiteral("1.-0"),
                                QStringLiteral("1.٢"), QStringLiteral("1.2garbage"),
                                QStringLiteral("1..2"), QStringLiteral("0.0"),
                                QStringLiteral(""), QStringLiteral(".")}) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        qsizetype suffixIndex = 0;
#else
        int suffixIndex = 0;
#endif
        const auto trimmed = input.trimmed();
        const auto parsed = QVersionNumber::fromString(trimmed, &suffixIndex);
        const auto reference = suffixIndex == trimmed.size() ? parsed : QVersionNumber();
        const auto actual = NativeVersion::parseNumeric(input.trimmed().toStdString());
        const auto segments = reference.segments();
        ok &= require(actual == NativeVersion::Segments(segments.cbegin(), segments.cend()),
                      QStringLiteral("native parsing differs for: %1").arg(input), err);
    }

    // Isolate the compatibility cache from the user's real settings.
    QTemporaryDir settingsDirectory;
    ok &= require(settingsDirectory.isValid(), QStringLiteral("isolated settings directory"), err);
    if (!settingsDirectory.isValid()) {
        return 1;
    }
    QCoreApplication::setOrganizationName(QStringLiteral("MoonlightTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SharedHelpers"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDirectory.path());
    QSettings settings;
    ok &= require(CompatFetcher::isGfeVersionSupported("99.0"),
                  QStringLiteral("missing compatibility data remains permissive"), err);
    settings.setValue("latestsupportedversion-v1", "3.27");
    ok &= require(CompatFetcher::isGfeVersionSupported("3.27.0"),
                  QStringLiteral("equivalent GFE version is supported"), err);
    ok &= require(CompatFetcher::isGfeVersionSupported("3.26.99"),
                  QStringLiteral("older GFE version is supported"), err);
    ok &= require(!CompatFetcher::isGfeVersionSupported("3.28"),
                  QStringLiteral("newer GFE version is rejected"), err);
    ok &= require(CompatFetcher::isGfeVersionSupported("3.28garbage"),
                  QStringLiteral("malformed GFE version remains permissive"), err);
    settings.setValue("latestsupportedversion-v1", "garbage");
    ok &= require(CompatFetcher::isGfeVersionSupported("99.0"),
                  QStringLiteral("malformed compatibility data remains permissive"), err);

    const QSslCertificate pinned = certificate("server.pem");
    const QSslCertificate other = certificate("other.pem");
    ok &= require(!pinned.isNull() && !other.isNull(), QStringLiteral("certificate fixtures load"),
                  err);
    ok &= require(PairedCertificate::matches(pinned, pinned),
                  QStringLiteral("paired leaf certificate matches"), err);
    ok &= require(!PairedCertificate::matches(pinned, other),
                  QStringLiteral("unpaired certificate is rejected"), err);
    ok &= require(!PairedCertificate::matches(QSslCertificate(), QSslCertificate()),
                  QStringLiteral("two missing certificates never match"), err);
    ok &= require(PairedCertificate::canIgnoreErrors(
                      pinned, { QSslError(QSslError::SelfSignedCertificate, pinned),
                                QSslError(QSslError::HostNameMismatch, pinned) }),
                  QStringLiteral("paired self-signed/IP-address TLS errors may be ignored"), err);
    ok &= require(!PairedCertificate::canIgnoreErrors(
                      pinned, { QSslError(QSslError::SelfSignedCertificate, pinned),
                                QSslError(QSslError::SelfSignedCertificate, other) }),
                  QStringLiteral("mixed certificate errors must not be ignored"), err);
    ok &= require(
        !PairedCertificate::canIgnoreErrors(pinned, { QSslError(QSslError::NoPeerCertificate) }),
        QStringLiteral("TLS errors without a certificate must not be ignored"), err);

    if (ok) {
        QTextStream(stdout) << "shared_helpers=passed\n";
    }
    return ok ? 0 : 1;
}
