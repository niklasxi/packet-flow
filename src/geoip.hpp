#pragma once
#include "packet.hpp"
#include <QObject>
#include <QUrl>
#include <map>
#include <memory>

struct MMDB_s;

struct IpLocation {
    QString country, city, flag;
    bool available() const { return !country.isEmpty() || !city.isEmpty(); }
    QString label() const;
};

class GeoIp : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString databaseName READ databaseName NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool usesDbIp READ usesDbIp NOTIFY changed)
  public:
    GeoIp();
    bool enabled() const { return bool(database); }
    QString databaseName() const;
    QString error() const { return message; }
    bool usesDbIp() const;
    void setEnabled(bool);
    Q_INVOKABLE bool chooseDatabase(const QUrl &);
    IpLocation lookup(pf::Address) const;
  signals:
    void changed();

  private:
    struct Close {
        void operator()(MMDB_s *) const;
    };
    std::unique_ptr<MMDB_s, Close> database;
    mutable std::map<pf::Address, IpLocation> cache;
    QString path, message;
    bool open(const QString &candidate);
};
