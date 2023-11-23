#include "geoip.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <maxminddb.h>
#include <netinet/in.h>
#include <algorithm>
#include <cstring>

using pf::Address;
namespace {
Address normalized(Address a) {
    const auto &b = a.value;
    if (a.family == Address::IPv6 && std::all_of(b.begin(), b.begin() + 10, [](auto c) { return c == 0; }) &&
        b[10] == 255 && b[11] == 255)
        return Address::read(Address::IPv4, std::span(b).subspan(12));
    return a;
}
bool publicUnicast(const Address &a) {
    const auto &b = a.value;
    if (a.family == Address::IPv6) {
        return (b[0] & 0xe0) == 0x20 && !(b[0] == 0x20 && b[1] == 1 && b[2] == 0x0d && b[3] == 0xb8) &&
               !(b[0] == 0x3f && b[1] == 0xff && (b[2] & 0xf0) == 0);
    }
    if (a.family != Address::IPv4 || b[0] == 0 || b[0] == 10 || b[0] == 127 || b[0] >= 224)
        return false;
    if (b[0] == 100 && (b[1] & 0xc0) == 64)
        return false;
    if (b[0] == 169 && b[1] == 254)
        return false;
    if (b[0] == 172 && (b[1] & 0xf0) == 16)
        return false;
    if (b[0] == 192 && (b[1] == 168 || (b[1] == 0 && (b[2] == 0 || b[2] == 2))))
        return false;
    if (b[0] == 198 && (b[1] == 18 || b[1] == 19 || (b[1] == 51 && b[2] == 100)))
        return false;
    return !(b[0] == 203 && b[1] == 0 && b[2] == 113);
}
QString field(MMDB_entry_s &entry, const char *section, const char *key, const char *language = nullptr) {
    const char *path[] = {section, key, language, nullptr};
    MMDB_entry_data_s data{};
    if (MMDB_aget_value(&entry, &data, path) != MMDB_SUCCESS || !data.has_data ||
        data.type != MMDB_DATA_TYPE_UTF8_STRING || data.data_size > 1024)
        return {};
    return QString::fromUtf8(data.utf8_string, data.data_size).simplified();
}
QString countryFlag(const QString &code) {
    if (code.size() != 2 || code == "ZZ" || code == "XX")
        return {};
    char32_t pair[2];
    for (int i = 0; i < 2; ++i) {
        auto ch = code[i].unicode();
        if (ch < 'A' || ch > 'Z')
            return {};
        pair[i] = 0x1f1e6 + ch - 'A';
    }
    return QString::fromUcs4(pair, 2);
}
QString bundledPath() {
#ifdef PACKETFLOW_BUNDLED_MMDB
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../Resources/GeoIP/" PACKETFLOW_BUNDLED_MMDB);
#else
    return {};
#endif
}
} // namespace
QString IpLocation::label() const {
    QStringList names;
    if (!city.isEmpty())
        names << city;
    if (!country.isEmpty() && country != city)
        names << country;
    if (names.isEmpty())
        return {};
    return (flag.isEmpty() ? QString() : flag + " ") + names.join(", ");
}
void GeoIp::Close::operator()(MMDB_s *db) const {
    MMDB_close(db);
    delete db;
}
GeoIp::GeoIp() {
    QSettings settings;
    path = settings.value("geoip/database", bundledPath()).toString();
    if (settings.value("geoip/enabled", false).toBool())
        open(path);
}
QString GeoIp::databaseName() const { return QFileInfo(path).fileName(); }
bool GeoIp::usesDbIp() const {
    return database &&
           QString::fromUtf8(database->metadata.database_type).startsWith("DBIP", Qt::CaseInsensitive);
}
bool GeoIp::open(const QString &candidate) {
    if (candidate.isEmpty() || !QFileInfo(candidate).isFile()) {
        message = "Choose a local City or Country .mmdb database to enable IP locations.";
        emit changed();
        return false;
    }
    auto fresh = std::make_unique<MMDB_s>();
    auto result = MMDB_open(QFile::encodeName(candidate).constData(), MMDB_MODE_MMAP, fresh.get());
    if (result != MMDB_SUCCESS) {
        message = "Could not open the database: " + QString::fromUtf8(MMDB_strerror(result));
        emit changed();
        return false;
    }
    std::unique_ptr<MMDB_s, Close> next(fresh.release());
    auto type = QString::fromUtf8(next->metadata.database_type);
    if (!type.contains("City", Qt::CaseInsensitive) && !type.contains("Country", Qt::CaseInsensitive)) {
        message = "Choose a City or Country database containing IP geolocation data.";
        emit changed();
        return false;
    }
    database = std::move(next);
    cache.clear();
    path = candidate;
    message.clear();
    return true;
}
void GeoIp::setEnabled(bool value) {
    if (value == enabled())
        return;
    if (value) {
        if (!open(path))
            return;
    } else {
        database.reset();
        cache.clear();
        message.clear();
    }
    QSettings().setValue("geoip/enabled", value);
    emit changed();
}
bool GeoIp::chooseDatabase(const QUrl &url) {
    if (!open(url.isLocalFile() ? url.toLocalFile() : QString()))
        return false;
    QSettings settings;
    settings.setValue("geoip/database", path);
    settings.setValue("geoip/enabled", true);
    emit changed();
    return true;
}
IpLocation GeoIp::lookup(Address addr) const {
    addr = normalized(addr);
    if (!database || !publicUnicast(addr))
        return {};
    if (auto it = cache.find(addr); it != cache.end())
        return it->second;
    int error = 0;
    MMDB_lookup_result_s result{};
    if (addr.family == Address::IPv4) {
        sockaddr_in ip{};
        ip.sin_family = AF_INET;
        std::memcpy(&ip.sin_addr, addr.value.data(), 4);
        result = MMDB_lookup_sockaddr(database.get(), reinterpret_cast<const sockaddr *>(&ip), &error);
    } else {
        sockaddr_in6 ip{};
        ip.sin6_family = AF_INET6;
        std::memcpy(&ip.sin6_addr, addr.value.data(), 16);
        result = MMDB_lookup_sockaddr(database.get(), reinterpret_cast<const sockaddr *>(&ip), &error);
    }
    IpLocation loc;
    if (error == MMDB_SUCCESS && result.found_entry) {
        auto code = field(result.entry, "country", "iso_code");
        loc.flag = countryFlag(code);
        loc.country = field(result.entry, "country", "names", "en");
        if (loc.country.isEmpty() && !loc.flag.isEmpty())
            loc.country = code;
        loc.city = field(result.entry, "city", "names", "en");
    }
    if (cache.size() >= 4096)
        cache.clear();
    cache.emplace(addr, loc);
    return loc;
}
