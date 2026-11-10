#include "nvpairingmanager.h"
#include "nativepairing.h"
#include <stdexcept>

namespace {
QString verifyPairingStage(QString response, const char* stage)
{
    try { NvHTTP::verifyResponseStatus(response); }
    catch (const GfeHttpResponseException& e) {
        throw GfeHttpResponseException(e.getStatusCode(),
            QStringLiteral("%1: %2").arg(QString::fromLatin1(stage), e.toQString()));
    }
    return response;
}
// Serialization/storage owner only. Remove with the remaining NvHTTP and
// settings Qt types; the protocol itself is shared with the native worker.
class PairingHost final : public NativePairing::Host {
public:
    PairingHost(NvHTTP& http, NvComputer& computer) : m_Http(http), m_Computer(computer) {}
    NativePairing::Response exchange(bool tls, const std::string& arguments, int timeoutMs, const char* stage) override
    {
        const auto xml = verifyPairingStage(m_Http.openConnectionToString(
            tls ? m_Http.m_BaseUrlHttps : m_Http.m_BaseUrlHttp, "pair",
            QString::fromStdString(arguments), timeoutMs), stage);
        auto bytes = [&](const char* field) {
            const auto data = NvHTTP::getXmlStringFromHex(xml, field);
            return NativePairing::Bytes(data.begin(), data.end());
        };
        NativePairing::Response response;
        response.paired = NvHTTP::getXmlString(xml, "paired") == "1";
        response.certificatePem = NvHTTP::getXmlStringFromHex(xml, "plaincert").toStdString();
        response.pairName = NvHTTP::getXmlString(xml, "pairname").toStdString();
        response.challengeResponse = bytes("challengeresponse");
        response.pairingSecret = bytes("pairingsecret");
        return response;
    }
    void unpair() override
    {
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", nullptr, 5000);
    }
    bool pinCertificate(const std::string& pem) override
    {
        m_Certificate = QSslCertificate(QByteArray(pem.data(), static_cast<int>(pem.size())));
        if (m_Certificate.isNull()) return false;
        m_Http.setServerCert(m_Certificate);
        return true;
    }
    void savePairName(const std::string& name) override
    {
        NvComputer::savePairname(m_Computer.uuid, QString::fromStdString(name));
    }
    const QSslCertificate& certificate() const { return m_Certificate; }
private:
    NvHTTP& m_Http;
    NvComputer& m_Computer;
    QSslCertificate m_Certificate;
};
}
NvPairingManager::NvPairingManager(NvComputer* computer) : m_Http(computer), m_Computer(computer) {}
NvPairingManager::~NvPairingManager() = default;

NvPairingManager::PairState NvPairingManager::pair(QString appVersion, QString pin, QSslCertificate& serverCert)
{
    PairingHost host(m_Http, *m_Computer);
    std::string pairedCertificate;
    NativePairing::State result;
    try {
        result = NativePairing::pair(host, NvHTTP::parseQuad(appVersion).at(0), pin.toStdString(),
            IdentityManager::get()->getCertificate().toStdString(), IdentityManager::get()->getPrivateKey().toStdString(),
            pairedCertificate);
    }
    catch (const GfeHttpResponseException&) { throw; }
    catch (const QtNetworkReplyException&) { throw; }
    catch (const std::runtime_error& error) {
        qCritical() << "Pairing cryptography failed:" << error.what();
        return FAILED;
    }
    switch (result) {
    case NativePairing::State::Paired: serverCert = host.certificate(); return PAIRED;
    case NativePairing::State::PinWrong: return PIN_WRONG;
    case NativePairing::State::AlreadyInProgress: return ALREADY_IN_PROGRESS;
    default: return FAILED;
    }
}
