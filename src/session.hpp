#pragma once
#include "capture.hpp"
#include "geoip.hpp"
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <functional>

class Session : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(GeoIp *geo READ geo CONSTANT)
    Q_PROPERTY(QString peerLocation READ peerLocation NOTIFY selectionChanged)
    Q_PROPERTY(QString source READ source NOTIFY stateChanged)
    Q_PROPERTY(QString name READ name NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(bool live READ live NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool finished READ finished NOTIFY stateChanged)
    Q_PROPERTY(bool reduced READ reduced WRITE setReduced NOTIFY stateChanged)
    Q_PROPERTY(int peerLimit READ peerLimit WRITE setPeerLimit NOTIFY mapOptionsChanged)
    Q_PROPERTY(int idleSeconds READ idleSeconds WRITE setIdleSeconds NOTIFY mapOptionsChanged)
    Q_PROPERTY(int mapGroups READ mapGroups WRITE setMapGroups NOTIFY mapOptionsChanged)
    Q_PROPERTY(QString mapGroupLabel READ mapGroupLabel NOTIFY mapOptionsChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY clockChanged)
    Q_PROPERTY(double duration READ duration NOTIFY countsChanged)
    Q_PROPERTY(double progress READ progress NOTIFY countsChanged)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY countsChanged)
    Q_PROPERTY(QVariantMap details READ details NOTIFY selectionChanged)
    Q_PROPERTY(QString peer READ peer NOTIFY selectionChanged)
    Q_PROPERTY(QString focus READ focus NOTIFY topologyChanged)
    Q_PROPERTY(QString filterLabel READ filterLabel NOTIFY filterChanged)
    Q_PROPERTY(qulonglong intervalSkipped READ intervalSkipped NOTIFY summaryChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY countsChanged)
  public:
    GeoIp *geo() { return &geoIp; }
    QString peerLocation() const;
    enum MapGroup { WebServers = 1, DnsServers = 2, OtherPeers = 4, AllGroups = 7 };
    Q_ENUM(MapGroup)
    int peerLimit() const { return limit; }
    int idleSeconds() const { return idleTimeout; }
    int mapGroups() const { return groups; }
    QString mapGroupLabel() const;
    void setPeerLimit(int value);
    void setIdleSeconds(int seconds);
    void setMapGroups(int value);
    bool hasEndpoint(pf::Address a) const { return counts.contains(a) || a == anchor || a == selectedPeer; }
    explicit Session(QObject *parent = nullptr,
                     std::unique_ptr<pf::CaptureBackend> backend = std::make_unique<pf::CaptureBackend>());
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString source() const { return kind; }
    QString name() const { return sourceName; }
    QString status() const;
    QString message() const { return notice; }
    bool live() const { return kind == "Live"; }
    bool busy() const { return worker && !live(); }
    bool running() const { return worker && live(); }
    bool playing() const { return play; }
    bool finished() const { return ended; }
    bool reduced() const { return reduce; }
    double speed() const { return rate; }
    double position() const { return double(playhead) / 1e9; }
    double duration() const {
        return store.data.empty() ? 0 : double(store.data.back().elapsed + pf::life) / 1e9;
    }
    double progress() const { return feed.progress; }
    QVariantMap totals() const;
    QVariantMap details() const { return inspected; }
    QString peer() const { return QString::fromStdString(selectedPeer.text()); }
    QString focus() const { return QString::fromStdString(anchor.text()); }
    QString filterLabel() const { return filterText; }
    qulonglong intervalSkipped() const { return intervalSkips; }
    QVariantList histogram() const { return bins; }
    void setSpeed(double);
    void setReduced(bool);
    Q_INVOKABLE void tryDemo();
    Q_INVOKABLE void openFile(const QUrl &);
    Q_INVOKABLE QVariantList listInterfaces();
    Q_INVOKABLE void startLive(const QString &);
    Q_INVOKABLE void stopSource();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void selectRow(int);
    Q_INVOKABLE int selectedRow() const;
    Q_INVOKABLE void selectPacket(qulonglong);
    Q_INVOKABLE void selectPeer(const QString &);
    Q_INVOKABLE void focusPeer(const QString &);
    Q_INVOKABLE QVariantList endpoints(const QString &query) const;
    Q_INVOKABLE QString applyFilter(QString proto, QString peer, QString dir, QString minimum,
                                    QString maximum, QString expr);
    Q_INVOKABLE void animateSelected();
    Q_INVOKABLE void exportCsv(const QUrl &);
    Q_INVOKABLE QVariantMap byteLine(int line) const;
    Q_INVOKABLE void copy(const QString &);
    Q_INVOKABLE void dismiss() {
        notice.clear();
        emit stateChanged();
    }
    Q_INVOKABLE QString protocolColor(QString name) const;
    struct Node {
        pf::Address address;
        QString label, addressText;
        int count;
        unsigned recent;
        bool local, focus, selected, related;
        int roles = OtherPeers;
        IpLocation location;
        QString subtitle(bool reducedMotion) const;
    };
    struct DrawArrow {
        pf::Arrow arrow;
        pf::Phase phase;
        bool selected, dim;
    };
    std::vector<Node> nodes() const;
    std::vector<DrawArrow> arrows() const;
    void previewAt(double seconds); // deterministic screenshot and presentation checks
  signals:
    void stateChanged();
    void countsChanged();
    void mapOptionsChanged();
    void selectionChanged();
    void topologyChanged();
    void clockChanged();
    void frame();
    void filterChanged();
    void summaryChanged();
    void inspectedPacket();
    void newSession();

  private:
    enum Role { Id = Qt::UserRole + 1, Time, From, To, Proto, Length, Summary, Color };
    GeoIp geoIp;
    pf::Store store;
    std::deque<uint64_t> rows;
    struct EndpointStats {
        // One timestamp per retained packet involving this address, in observation order.
        QList<int64_t> observations;
        int web = 0, dns = 0;
        int packets() const { return int(observations.size()); }
        int64_t lastSeenAt(int64_t time) const;
        int roles() const {
            return web || dns ? (web ? WebServers : 0) | (dns ? DnsServers : 0) : OtherPeers;
        }
    };
    int limit = 4, groups = AllGroups, idleTimeout = 0;
    int64_t liveViewTime = 0, oldestVisibleActivity = -1;
    int64_t activityTime() const { return live() ? liveViewTime : playhead; }
    bool visiblePeerExpired() const;
    std::array<int, AllGroups + 1> roleCounts{};
    std::map<pf::Address, EndpointStats> counts;
    bool matchesMapGroup(pf::Address) const;
    int matchingAddresses() const;
    bool eligibleOnMap(pf::Address) const;
    void updateMapOptions();
    std::map<pf::Address, unsigned> recentActivity;
    std::set<pf::Address> locals;
    std::vector<pf::Address> slotsOnMap;
    std::set<pf::Address> mapAddresses;
    pf::Address anchor, selectedPeer;
    uint64_t selected = 0;
    pf::Filter filter, extra;
    pf::Scheduler scheduler;
    std::optional<pf::Arrow> solo;
    int64_t soloStart = 0, playhead = 0, visualTime = 0, lastTick = 0;
    size_t next = 0;
    uint64_t hiddenSkipped = 0, intervalSkips = 0;
    int64_t intervalStart = 0, lastBins = 0;
    double rate = 1;
    bool play = false, ended = false, reduce = false, stopping = false;
    std::function<void()> pendingSwitch;
    QString kind, sourceName, notice, filterText;
    QVariantMap inspected;
    QVariantList bins;
    QElapsedTimer clock;
    QTimer ticks;
    std::unique_ptr<pf::CaptureBackend> backend;
    std::unique_ptr<pf::Source> worker;
    pf::Feed feed;
    std::vector<pf::Interface> devices;
    bool finishSourceBefore(std::function<void()>);
    void reset(QString type, QString label);
    void tick();
    void drain();
    void append(std::vector<pf::Packet> batch);
    void refreshCounts(const pf::Packet &, int delta, int64_t observed = 0);
    void rebuildRows();
    void layout(bool fresh = false);
    void rebuildBins();
    void inspect();
    void reconstruct();
    void schedule(size_t begin, size_t end, bool liveBatch = false);
    bool accepts(const pf::Packet &) const;
    pf::Address mapped(pf::Address) const;
    QString alias(pf::Address) const;
    void fail(QString reason) {
        notice = reason;
        emit stateChanged();
    }
};
