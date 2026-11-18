#include "hostprofile.h"
#include "legacyworkerprofile.h"
#include "nvcomputer.h"
#include "pairedcertificate.h"
#include "windowsprofilefile.h"

#include <nlohmann/json.hpp>
#include <climits>
#include <mutex>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
constexpr size_t MaxHostProfileBytes = 64 * 1024 * 1024;

QString string(const Json& object, const char* key)
{
    const auto& value = object.at(key).get_ref<const std::string&>();
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

int integer(const Json& object, const char* key, int min, int max)
{
    const auto& value = object.at(key);
    if (!value.is_number_integer()) throw std::runtime_error("Invalid host profile integer");
    const auto number = value.get<double>();
    if (number < min || number > max) throw std::runtime_error("Host profile integer out of range");
    return static_cast<int>(number);
}

Json encodeAddress(const NvAddress& address)
{
    return {{"host", address.address().toUtf8().toStdString()}, {"port", address.port()}};
}

NvAddress decodeAddress(const Json& value)
{
    return NvAddress(string(value, "host"), integer(value, "port", 0, UINT16_MAX));
}

Json encode(const NvComputer& host)
{
    auto apps = Json::array();
    for (const auto& app : host.appList) {
        apps.push_back({{"id", app.id}, {"name", app.name.toUtf8().toStdString()},
            {"hdr", app.hdrSupported}, {"app_collector", app.isAppCollectorGame},
            {"hidden", app.hidden}, {"direct_launch", app.directLaunch}});
    }
    return {{"uuid", host.uuid.toUtf8().toStdString()}, {"name", host.name.toUtf8().toStdString()},
        {"custom_name", host.hasCustomName}, {"mac", host.macAddress.toHex().toStdString()},
        {"local", encodeAddress(host.localAddress)}, {"remote", encodeAddress(host.remoteAddress)},
        {"ipv6", encodeAddress(host.ipv6Address)}, {"manual", encodeAddress(host.manualAddress)},
        {"server_certificate", PairedCertificate::toOptionalPem(host.serverCert).toStdString()},
        {"nvidia", host.isNvidiaServerSoftware}, {"apps", std::move(apps)}};
}

std::unique_ptr<NvComputer> decode(const Json& value)
{
    auto host = std::make_unique<NvComputer>();
    host->uuid = string(value, "uuid");
    host->name = string(value, "name");
    if (host->uuid.isEmpty()) throw std::runtime_error("Missing host UUID");
    host->hasCustomName = value.at("custom_name").get<bool>();
    const auto mac = string(value, "mac").toLatin1();
    host->macAddress = QByteArray::fromHex(mac);
    if (host->macAddress.toHex() != mac) throw std::runtime_error("Invalid host MAC");
    host->localAddress = decodeAddress(value.at("local"));
    host->remoteAddress = decodeAddress(value.at("remote"));
    host->ipv6Address = decodeAddress(value.at("ipv6"));
    host->manualAddress = decodeAddress(value.at("manual"));
    auto cert = string(value, "server_certificate").toLatin1();
    // Older Qt TLS backends could write this for an unpaired host.
    if (cert == "-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n") {
        cert.clear();
    }
    host->serverCert = HostCertificate(cert);
    if (!cert.isEmpty() && host->serverCert.isNull()) {
        throw std::runtime_error("Invalid paired host certificate");
    }
    host->isNvidiaServerSoftware = value.at("nvidia").get<bool>();
    for (const auto& entry : value.at("apps").get_ref<const Json::array_t&>()) {
        NvApp app;
        app.id = integer(entry, "id", INT_MIN, INT_MAX);
        app.name = string(entry, "name");
        app.hdrSupported = entry.at("hdr").get<bool>();
        app.isAppCollectorGame = entry.at("app_collector").get<bool>();
        app.hidden = entry.at("hidden").get<bool>();
        app.directLaunch = entry.at("direct_launch").get<bool>();
        host->appList.append(std::move(app));
    }
    return host;
}

constexpr size_t MaxPairNamesBytes = 4 * 1024 * 1024;
std::mutex pairNamesMutex;
std::optional<Json> cachedPairNames;

Json loadPairNames(const std::filesystem::path& path)
{
    const auto data = WindowsProfileFile::read(path, MaxPairNamesBytes);
    if (!data) {
        // Legacy import is read-only. Never mix a later INI update into JSON.
        auto names = Json::object();
        constexpr std::string_view group = "Pairnames/";
        for (const auto& [key, name] : LegacyWorkerProfile::read(path.parent_path())) {
            if (key.compare(0, group.size(), group) == 0) names[key.substr(group.size())] = name;
        }
        return names;
    }
    auto profile = Json::parse(*data);
    integer(profile, "version", 1, 1);
    auto& names = profile.at("names");
    for (const auto& entry : names.get_ref<const Json::object_t&>()) {
        if (!entry.second.is_string()) throw std::runtime_error("Invalid pair name");
    }
    return std::move(names);
}
}

std::optional<std::vector<std::unique_ptr<NvComputer>>> HostProfile::read(const std::filesystem::path& path)
{
    const auto data = WindowsProfileFile::read(path, MaxHostProfileBytes);
    if (!data) return std::nullopt;
    const auto profile = Json::parse(*data);
    integer(profile, "version", 1, 1);
    std::vector<std::unique_ptr<NvComputer>> hosts;
    for (const auto& entry : profile.at("hosts").get_ref<const Json::array_t&>()) {
        hosts.push_back(decode(entry));
    }
    return hosts;
}

std::vector<std::unique_ptr<NvComputer>> HostProfile::importLegacy(const std::filesystem::path& directory)
{
    using namespace LegacyWorkerProfile;
    const auto& values = LegacyWorkerProfile::read(directory);
    std::string group = "hostsbackup/";
    int count = number(values, group + "size");
    if (count == 0) {
        group = "hosts/";
        count = number(values, group + "size");
    }
    if (count < 0 || static_cast<size_t>(count) > values.size()) {
        throw std::runtime_error("Invalid legacy host count");
    }
    std::vector<std::unique_ptr<NvComputer>> hosts;
    for (int i = 1; i <= count; ++i) {
        const auto prefix = group + std::to_string(i) + '/';
        const auto text = [&](const char* key) { return QString::fromUtf8(value(values, prefix + key)); };
        const auto bytes = [&](const char* key) {
            const auto data = value(values, prefix + key);
            return QByteArray(data.data(), static_cast<qsizetype>(data.size()));
        };
        const auto boolean = [&](const char* key) {
            return QVariant(text(key)).toBool();
        };
        const auto address = [&](const char* host, const char* port) {
            return NvAddress(text(host), number(values, prefix + port, DEFAULT_HTTP_PORT));
        };
        auto host = std::make_unique<NvComputer>();
        host->name = text("hostname");
        host->uuid = text("uuid");
        if (host->uuid.isEmpty()) throw std::runtime_error("Missing legacy host UUID");
        host->hasCustomName = boolean("customname");
        host->macAddress = bytes("mac");
        host->localAddress = address("localaddress", "localport");
        host->remoteAddress = address("remoteaddress", "remoteport");
        host->ipv6Address = address("ipv6address", "ipv6port");
        host->manualAddress = address("manualaddress", "manualport");
        host->serverCert = HostCertificate(bytes("srvcert"));
        host->isNvidiaServerSoftware = boolean("nvidiasw");
        const auto appGroup = prefix + "apps/";
        const auto appCount = number(values, appGroup + "size");
        if (appCount < 0 || static_cast<size_t>(appCount) > values.size()) {
            throw std::runtime_error("Invalid legacy app count");
        }
        for (int j = 1; j <= appCount; ++j) {
            const auto entry = appGroup + std::to_string(j) + '/';
            NvApp app;
            app.id = number(values, entry + "id");
            app.name = QString::fromUtf8(value(values, entry + "name"));
            app.hdrSupported = QVariant(QString::fromUtf8(value(values, entry + "hdr"))).toBool();
            app.isAppCollectorGame = QVariant(QString::fromUtf8(value(values, entry + "appcollector"))).toBool();
            app.hidden = QVariant(QString::fromUtf8(value(values, entry + "hidden"))).toBool();
            app.directLaunch = QVariant(QString::fromUtf8(value(values, entry + "directlaunch"))).toBool();
            host->appList.append(std::move(app));
        }
        hosts.push_back(std::move(host));
    }
    return hosts;
}

void HostProfile::save(const std::filesystem::path& path, const std::vector<NvComputer>& hosts)
{
    auto entries = Json::array();
    for (const auto& host : hosts) entries.push_back(encode(host));
    const auto data = Json{{"version", 1}, {"hosts", std::move(entries)}}.dump();
    if (data.size() > MaxHostProfileBytes) throw std::runtime_error("Host profile too large");
    WindowsProfileFile::replace(path, data);
}

QString HostProfile::pairName(const QString& directory, const QString& uuid)
{
    std::lock_guard lock(pairNamesMutex);
    const auto path = std::filesystem::path(directory.toStdWString()) / L"pair-names-v1.json";
    if (!cachedPairNames) cachedPairNames = loadPairNames(path);
    const auto& names = *cachedPairNames;
    const auto key = uuid.toUtf8().toStdString();
    const auto entry = names.find(key);
    if (entry == names.end()) return {};
    return QString::fromStdString(entry->get_ref<const std::string&>());
}

void HostProfile::setPairName(const QString& directory, const QString& uuid, const QString& name)
{
    std::lock_guard lock(pairNamesMutex);
    const auto path = std::filesystem::path(directory.toStdWString()) / L"pair-names-v1.json";
    if (!cachedPairNames) cachedPairNames = loadPairNames(path);
    auto names = *cachedPairNames;
    names[uuid.toUtf8().toStdString()] = name.toUtf8().toStdString();
    const auto data = Json{{"version", 1}, {"names", names}}.dump();
    if (data.size() > MaxPairNamesBytes) throw std::runtime_error("Pair name profile too large");
    WindowsProfileFile::replace(path, data);
    cachedPairNames = std::move(names);
}
