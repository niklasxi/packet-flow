#include "session.hpp"
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QSettings>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <iterator>

using namespace pf;
namespace {
QString qs(const std::string &s) { return QString::fromStdString(s); }
QString address(Address a) { return a ? qs(a.text()) : QStringLiteral("Unknown"); }
QString protocol(const Packet &p) { return QString::fromLatin1(style(p.proto).name); }
QString number(uint64_t n) { return QString::number(n); }
int serviceRoles(Protocol proto, std::optional<uint16_t> port) {
    if ((proto == Protocol::TCP && port == 80) ||
        ((proto == Protocol::TCP || proto == Protocol::UDP) && port == 443))
        return Session::WebServers;
    if (proto == Protocol::DNS && port == 53)
        return Session::DnsServers;
    return 0;
}
QString endpointRoleLabel(int roles) {
    switch (roles) {
    case Session::WebServers: return QStringLiteral("Likely web server");
    case Session::DnsServers: return QStringLiteral("DNS server");
    case Session::WebServers | Session::DnsServers: return QStringLiteral("DNS + likely web");
    default: return QStringLiteral("Role unknown");
    }
}
QString roleLabel(int roles) {
    QStringList names;
    if (roles & Session::WebServers)
        names << "Web servers";
    if (roles & Session::DnsServers)
        names << "DNS servers";
    if (roles & Session::OtherPeers)
        names << "Other endpoints";
    return names.join(" + ");
}
} // namespace
Session::Session(QObject *parent, std::unique_ptr<CaptureBackend> captureBackend)
    : QAbstractListModel(parent), backend(std::move(captureBackend)) {
    connect(&geoIp, &GeoIp::changed, this, [this] {
        emit topologyChanged();
        emit selectionChanged();
        emit countsChanged();
    });
    clock.start();
    reduce = QSettings().value("reduceMotion", false).toBool();
    limit = std::clamp(QSettings().value("mapPeerLimit", 4).toInt(), 4, 100);
    idleTimeout = std::clamp(QSettings().value("mapIdleSeconds", 0).toInt(), 0, 120);
    ticks.setTimerType(Qt::PreciseTimer);
    ticks.setInterval(16);
    connect(&ticks, &QTimer::timeout, this, &Session::tick);
    tryDemo();
    ticks.start();
}
int Session::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(rows.size()); }
QHash<int, QByteArray> Session::roleNames() const {
    return {{Id, "pid"},         {Time, "elapsed"},     {From, "src"},        {To, "dst"},
            {Proto, "protocol"}, {Length, "sizeLabel"}, {Summary, "summary"}, {Color, "tint"}};
}
QVariant Session::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= rows.size())
        return {};
    auto *p = store.get(rows[index.row()]);
    if (!p)
        return {};
    switch (role) {
    case Id:
        return number(p->id);
    case Time:
        return QString::number(double(p->elapsed) / 1e9, 'f', 3);
    case From:
        return address(p->from);
    case To:
        return address(p->to);
    case Proto:
        return protocol(*p);
    case Length:
        return QString::number(p->length());
    case Summary:
        return qs(p->summary);
    case Color:
        return style(p->proto).color;
    default:
        return {};
    }
}
QString Session::status() const {
    if (stopping)
        return live() ? "Stopping capture" : "Cancelling import";
    if (feed.state == State::Error)
        return "Source error";
    if (worker && feed.state == State::Starting)
        return "Starting";
    if (busy())
        return "Loading capture";
    if (live())
        return running() ? (play ? "Capturing" : "View paused · capturing") : "Capture stopped";
    return ended ? "Finished" : play ? "Playing" : "Paused";
}
void Session::reset(QString type, QString label) {
    worker.reset();
    beginResetModel();
    store = {};
    rows.clear();
    counts.clear();
    liveViewTime = 0;
    oldestVisibleActivity = -1;
    roleCounts.fill(0);
    groups = AllGroups;
    recentActivity.clear();
    slotsOnMap.clear();
    mapAddresses.clear();
    selected = 0;
    selectedPeer = {};
    anchor = {};
    locals.clear();
    filter = {};
    extra = {};
    filterText.clear();
    inspected.clear();
    bins.clear();
    scheduler = {};
    hiddenSkipped = 0;
    solo.reset();
    feed = {};
    next = 0;
    intervalSkips = 0;
    intervalStart = 0;
    lastBins = 0;
    emit summaryChanged();
    playhead = visualTime = 0;
    lastTick = clock.nsecsElapsed();
    play = ended = false;
    kind = type;
    sourceName = label;
    notice.clear();
    endResetModel();
    emit newSession();
    emit mapOptionsChanged();
    emit filterChanged();
    emit selectionChanged();
    emit topologyChanged();
    emit stateChanged();
    emit countsChanged();
    emit clockChanged();
    emit frame();
}
bool Session::finishSourceBefore(std::function<void()> action) {
    if (!worker)
        return false;
    pendingSwitch = std::move(action);
    stopping = true;
    worker->requestStop();
    emit stateChanged();
    drain();
    return true;
}
void Session::tryDemo() {
    if (finishSourceBefore([this] { tryDemo(); }))
        return;
    reset("Demo", "Demo session");
    anchor = Address::parse("192.0.2.24");
    locals = {anchor, Address::parse("2001:db8::24")};
    append(demo());
    layout(true);
    selectPacket(1);
    play = true;
    lastTick = clock.nsecsElapsed();
    reconstruct();
    emit stateChanged();
    emit frame();
}
void Session::openFile(const QUrl &url) {
    if (!url.isLocalFile())
        return fail("Choose a local capture file.");
    if (finishSourceBefore([this, url] { openFile(url); }))
        return;
    reset("File", QFileInfo(url.toLocalFile()).fileName());
    worker = backend->open(url.toLocalFile().toStdString(), false);
    emit stateChanged();
}
QVariantList Session::listInterfaces() {
    std::string error;
    devices = backend->devices(error);
    if (!error.empty())
        fail(qs(error));
    QVariantList out;
    for (auto &d : devices) {
        QStringList addrs;
        for (auto a : d.addresses)
            addrs << address(a);
        out << QVariantMap{
            {"name", qs(d.name)}, {"description", qs(d.description)}, {"addresses", addrs.join(" · ")}};
    }
    return out;
}
void Session::startLive(const QString &name) {
    auto it = std::find_if(devices.begin(), devices.end(), [&](auto &d) { return qs(d.name) == name; });
    if (it == devices.end())
        return fail("Select an available interface.");
    if (finishSourceBefore([this, name] { startLive(name); }))
        return;
    reset("Live", name);
    locals = it->addresses;
    if (!locals.empty())
        anchor = *locals.begin();
    worker = backend->open(it->name, true);
    play = true;
    emit stateChanged();
    emit topologyChanged();
}
void Session::stopSource() {
    if (!worker)
        return;
    pendingSwitch = {};
    stopping = true;
    worker->requestStop();
    emit stateChanged();
    drain();
}
int64_t Session::EndpointStats::lastSeenAt(int64_t time) const {
    if (!observations.isEmpty() && observations.back() <= time)
        return observations.back();
    auto next = std::upper_bound(observations.cbegin(), observations.cend(), time);
    return next == observations.cbegin() ? -1 : *std::prev(next);
}
void Session::refreshCounts(const Packet &p, int d, int64_t observed) {
    auto change = [&](Address a, int roles) {
        if (!a)
            return;
        auto &stats = counts[a];
        if (stats.packets())
            --roleCounts[stats.roles()];
        if (d > 0)
            stats.observations.append(observed);
        else
            stats.observations.removeFirst();
        if (roles & WebServers)
            stats.web += d;
        if (roles & DnsServers)
            stats.dns += d;
        if (stats.packets())
            ++roleCounts[stats.roles()];
        else
            counts.erase(a);
    };
    int fromRoles = serviceRoles(p.proto, p.sport), toRoles = serviceRoles(p.proto, p.dport);
    if (p.from == p.to)
        change(p.from, fromRoles | toRoles);
    else {
        change(p.from, fromRoles);
        change(p.to, toRoles);
    }
}
void Session::append(std::vector<Packet> batch) {
    if (batch.empty())
        return;
    size_t incoming = 0;
    for (auto &p : batch)
        incoming += p.bytes.size();
    size_t remove = 0, bytes = store.bytes;
    while (remove < store.data.size() && (store.data.size() - remove + batch.size() > store.limits.records ||
                                          bytes + incoming > store.limits.bytes)) {
        bytes -= store.data[remove++].bytes.size();
    }
    if (remove) {
        auto cutoff = store.data[remove - 1].id;
        int n = int(std::upper_bound(rows.begin(), rows.end(), cutoff) - rows.begin());
        if (n)
            beginRemoveRows({}, 0, n - 1);
        rows.erase(rows.begin(), rows.begin() + n);
        for (size_t i = 0; i < remove; ++i) {
            auto &p = store.data.front();
            refreshCounts(p, -1);
            store.bytes -= p.bytes.size();
            store.data.pop_front();
            ++store.evicted;
        }
        if (n)
            endRemoveRows();
        if (selected && selected <= cutoff) {
            selected = 0;
            inspect();
            notice = "The selected packet expired from retained history.";
            emit stateChanged();
        }
    }
    std::vector<uint64_t> added;
    const bool liveBatch = live();
    const auto arrival = clock.nsecsElapsed();
    if (liveBatch && play)
        liveViewTime = arrival;
    for (auto &p : batch) {
        if (accepts(p))
            added.push_back(p.id);
        refreshCounts(p, 1, liveBatch ? arrival : p.elapsed);
        store.append(std::move(p));
    }
    if (!added.empty()) {
        beginInsertRows({}, int(rows.size()), int(rows.size() + added.size() - 1));
        rows.insert(rows.end(), added.begin(), added.end());
        endInsertRows();
    }
    layout();
    if (kind == "Demo" || (live() && clock.nsecsElapsed() - lastBins >= 250 * ms)) {
        rebuildBins();
        lastBins = clock.nsecsElapsed();
    }
    emit countsChanged();
}
void Session::drain() {
    if (!worker)
        return;
    auto old = feed.state;
    auto before = feed.delivered;
    feed = worker->take();
    size_t n = feed.packets.size();
    if (n) {
        append(std::move(feed.packets));
        if (live() && play) {
            playhead = store.data.back().elapsed;
            schedule(store.data.size() - n, store.data.size(), true);
        }
    }
    if (feed.state == State::Finished || feed.state == State::Limited || feed.state == State::Error ||
        feed.state == State::Stopped) {
        worker.reset();
        stopping = false;
        if (pendingSwitch) {
            auto action = std::move(pendingSwitch);
            pendingSwitch = {};
            action();
            return;
        }
        notice = qs(feed.message);
        play = false;
        rebuildBins();
        if (!live()) {
            layout(true);
            if (!store.data.empty()) {
                selectPacket(store.data.front().id);
                reconstruct();
            }
        }
        if (feed.state == State::Stopped && !live())
            notice = "Import cancelled. Loaded records are available.";
        emit stateChanged();
        emit countsChanged();
        emit clockChanged();
        emit frame();
    } else if (old != feed.state || before != feed.delivered) {
        emit stateChanged();
        if (!n)
            emit countsChanged();
    }
}
bool Session::accepts(const Packet &p) const {
    auto focusSet = anchor && !locals.contains(anchor) ? &anchor : nullptr;
    if (!focusSet)
        return filter.accepts(p, locals) && extra.accepts(p, locals);
    // An explicit focus changes the frame of reference for the direction predicate.
    Filter without = filter;
    without.dir = Direction::Any;
    bool out = p.from == *focusSet, in = p.to == *focusSet;
    auto dir = out == in ? Direction::Other : out ? Direction::Out : Direction::In;
    return (filter.dir == Direction::Any || filter.dir == dir) && without.accepts(p, locals) &&
           extra.accepts(p, locals);
}
void Session::rebuildRows() {
    beginResetModel();
    rows.clear();
    for (auto &p : store.data)
        if (accepts(p))
            rows.push_back(p.id);
    endResetModel();
    emit countsChanged();
}
Address Session::mapped(Address a) const {
    return anchor && locals.contains(anchor) && locals.contains(a) ? anchor : a;
}
QString Session::alias(Address a) const {
    if (locals.contains(a))
        return "This computer";
    if (kind == "Demo") {
        static const std::map<Address, QString> names = {{Address::parse("192.0.2.53"), "DNS resolver"},
                                                         {Address::parse("203.0.113.42"), "Web server"},
                                                         {Address::parse("192.0.2.1"), "Local gateway"},
                                                         {Address::parse("2001:db8::8"), "IPv6 peer"}};
        auto it = names.find(a);
        if (it != names.end())
            return it->second;
    }
    return address(a);
}
QString Session::mapGroupLabel() const { return groups == AllGroups ? "All endpoints" : roleLabel(groups); }
bool Session::matchesMapGroup(Address a) const {
    if (groups == AllGroups)
        return true;
    auto it = counts.find(a);
    return it != counts.end() && (it->second.roles() & groups);
}
bool Session::eligibleOnMap(Address a) const {
    if (!a)
        return false;
    if (a == anchor)
        return true;
    if (!matchesMapGroup(a))
        return false;
    if (!idleTimeout)
        return true;
    auto it = counts.find(a);
    auto now = activityTime();
    auto last = it == counts.end() ? -1 : it->second.lastSeenAt(now);
    return last >= 0 && now - last < idleTimeout * 1000 * ms;
}
bool Session::visiblePeerExpired() const {
    return oldestVisibleActivity >= 0 && activityTime() - oldestVisibleActivity >= idleTimeout * 1000 * ms;
}
int Session::matchingAddresses() const {
    int total = 0;
    for (int roles = 1; roles <= AllGroups; ++roles)
        if (roles & groups)
            total += roleCounts[roles];
    return total;
}
void Session::updateMapOptions() {
    layout();
    if (!live() && !busy())
        reconstruct();
    emit mapOptionsChanged();
    emit countsChanged();
    emit frame();
}
void Session::setPeerLimit(int value) {
    value = std::clamp(value, 4, 100);
    if (value == limit)
        return;
    limit = value;
    QSettings().setValue("mapPeerLimit", limit);
    updateMapOptions();
}
void Session::setMapGroups(int value) {
    value &= AllGroups;
    if (!value)
        value = AllGroups;
    if (value == groups)
        return;
    groups = value;
    updateMapOptions();
}
void Session::setIdleSeconds(int seconds) {
    seconds = std::clamp(seconds, 0, 120);
    if (seconds == idleTimeout)
        return;
    idleTimeout = seconds;
    QSettings().setValue("mapIdleSeconds", seconds);
    updateMapOptions();
}
void Session::layout(bool fresh) {
    const size_t maxNodes = size_t(limit) + (anchor ? 1 : 0);
    if (fresh)
        slotsOnMap.clear();
    auto add = [&](Address a) {
        a = mapped(a);
        if (slotsOnMap.size() < maxNodes && eligibleOnMap(a) && mapAddresses.insert(a).second)
            slotsOnMap.push_back(a);
    };
    std::erase_if(slotsOnMap, [&](Address a) {
        return !eligibleOnMap(a) || (a != anchor && a != selectedPeer && !counts.contains(a));
    });
    if (slotsOnMap.size() > maxNodes)
        slotsOnMap.resize(maxNodes);
    mapAddresses = {slotsOnMap.begin(), slotsOnMap.end()};
    add(anchor);
    if (kind == "Demo")
        for (auto s : {"192.0.2.53", "203.0.113.42", "192.0.2.1", "2001:db8::8"})
            add(Address::parse(s));
    if (slotsOnMap.size() < maxNodes) {
        const size_t vacancies = maxNodes - slotsOnMap.size();
        size_t represented = 0;
        for (auto addr : slotsOnMap) {
            if (addr == anchor && locals.contains(anchor)) {
                for (auto local : locals)
                    if (counts.contains(local) && matchesMapGroup(local))
                        ++represented;
            } else if (counts.contains(addr) && matchesMapGroup(addr))
                ++represented;
        }
        if (size_t(matchingAddresses()) > represented) {
            std::vector<std::pair<int, Address>> ranked;
            for (auto &[addr, stats] : counts)
                if (!mapAddresses.contains(mapped(addr)) && eligibleOnMap(mapped(addr)))
                    ranked.emplace_back(stats.packets(), addr);
            auto stop = ranked.begin() + std::min(vacancies, ranked.size());
            std::partial_sort(ranked.begin(), stop, ranked.end(), [](auto &a, auto &b) {
                return a.first != b.first ? a.first > b.first : a.second < b.second;
            });
            for (auto it = ranked.begin(); it != stop; ++it)
                add(it->second);
        }
    }
    std::vector<Address> pinned;
    auto pin = [&](Address a) {
        a = mapped(a);
        if (!eligibleOnMap(a))
            return;
        pinned.push_back(a);
        if (mapAddresses.contains(a))
            return;
        if (slotsOnMap.size() < maxNodes)
            return add(a);
        auto it = std::find_if(slotsOnMap.rbegin(), slotsOnMap.rend(), [&](Address k) {
            return std::find(pinned.begin(), pinned.end(), k) == pinned.end();
        });
        if (it != slotsOnMap.rend()) {
            mapAddresses.erase(*it);
            mapAddresses.insert(a);
            *it = a;
        }
    };
    pin(anchor);
    pin(selectedPeer);
    if (auto *p = store.get(selected)) {
        pin(p->from);
        pin(p->to);
    }
    auto hidden = [&](const Arrow &a) {
        return !mapAddresses.contains(a.from) || !mapAddresses.contains(a.to);
    };
    std::erase_if(scheduler.active, hidden);
    if (solo && hidden(*solo))
        solo.reset();
    oldestVisibleActivity = -1;
    if (idleTimeout) {
        for (auto a : slotsOnMap) {
            if (a == anchor)
                continue;
            auto last = counts.at(a).lastSeenAt(activityTime());
            if (oldestVisibleActivity < 0 || last < oldestVisibleActivity)
                oldestVisibleActivity = last;
        }
    }
    emit topologyChanged();
}
QString Session::Node::subtitle(bool reducedMotion) const {
    if (reducedMotion)
        return recent ? QString::number(recent) + " recent" : QString::number(count) + " retained";
    if (label != addressText)
        return addressText;
    if (focus && roles == OtherPeers)
        return QStringLiteral("Focus endpoint");
    return endpointRoleLabel(roles);
}
std::vector<Session::Node> Session::nodes() const {
    std::vector<Node> v;
    auto *p = store.get(selected);
    for (auto a : slotsOnMap) {
        auto stats = counts.find(a);
        int count = stats == counts.end() ? 0 : stats->second.packets();
        int roles = stats == counts.end() ? OtherPeers : stats->second.roles();
        if (locals.contains(a) && a == anchor) {
            count = 0;
            for (auto l : locals)
                if (counts.contains(l))
                    count += counts.at(l).packets();
        }
        v.push_back({a, alias(a), address(a), count, recentActivity.contains(a) ? recentActivity.at(a) : 0,
                     locals.contains(a), a == anchor, a == mapped(selectedPeer),
                     p && (mapped(p->from) == a || mapped(p->to) == a), roles, geoIp.lookup(a)});
    }
    return v;
}
std::vector<Session::DrawArrow> Session::arrows() const {
    std::vector<DrawArrow> out;
    auto add = [&](const Arrow &a, int64_t age) {
        auto ph = phase(age);
        if (ph.opacity <= 0)
            return;
        auto peer = mapped(selectedPeer);
        out.push_back({a, ph, a.id == selected, peer && peer != a.from && peer != a.to});
    };
    if (!reduce) {
        for (auto &a : scheduler.active)
            if (!solo || solo->id != a.id)
                add(a, visualTime - a.start);
        if (solo)
            add(*solo, clock.nsecsElapsed() - soloStart);
    }
    return out;
}
void Session::schedule(size_t begin, size_t end, bool liveBatch) {
    if (reduce) {
        for (size_t i = begin; i < end; ++i) {
            const auto &packet = store.data[i];
            if (!accepts(packet))
                continue;
            auto from = mapped(packet.from), to = mapped(packet.to);
            if (groups != AllGroups && (!eligibleOnMap(from) || !eligibleOnMap(to)))
                continue;
            if (mapAddresses.contains(from))
                ++recentActivity[from];
            if (to != from && mapAddresses.contains(to))
                ++recentActivity[to];
        }
        if (begin != end)
            emit topologyChanged();
        return;
    }
    auto priorSkipped = scheduler.skipped + hiddenSkipped;
    std::vector<Event> events;
    for (size_t i = begin; i < end; ++i) {
        auto &p = store.data[i];
        if (!accepts(p))
            continue;
        auto a = mapped(p.from), b = mapped(p.to);
        if (groups != AllGroups && (!eligibleOnMap(a) || !eligibleOnMap(b)))
            continue;
        if (!mapAddresses.contains(a) || !mapAddresses.contains(b)) {
            ++hiddenSkipped;
            continue;
        }
        events.push_back({p.id, a, b, p.proto, p.length(), liveBatch ? visualTime : p.elapsed});
    }
    scheduler.admit(events, visualTime);
    auto added = scheduler.skipped + hiddenSkipped - priorSkipped;
    if (added) {
        intervalSkips += added;
        emit summaryChanged();
        emit countsChanged();
    }
}
void Session::reconstruct() {
    scheduler.active.clear();
    solo.reset();
    visualTime = playhead;
    intervalStart = visualTime;
    recentActivity.clear();
    intervalSkips = 0;
    emit summaryChanged();
    next = afterTime(store.data, playhead);
    auto start = afterTime(store.data, playhead - life);
    // Bound catch-up work even when a file contains many identical timestamps.
    start = std::max(start, next > 1024 ? next - 1024 : 0);
    auto old = scheduler.skipped, hidden = hiddenSkipped;
    schedule(start, next);
    scheduler.skipped = old;
    hiddenSkipped = hidden;
    emit countsChanged();
    if (reduce)
        emit topologyChanged();
    emit frame();
}
void Session::tick() {
    auto now = clock.nsecsElapsed();
    int64_t dt = now - lastTick;
    lastTick = now;
    if (live() && play) {
        liveViewTime = now;
        visualTime += dt;
    }
    if (visualTime - intervalStart >= 1000 * ms) {
        intervalStart = visualTime;
        if (!recentActivity.empty()) {
            recentActivity.clear();
            emit topologyChanged();
        }
        if (intervalSkips) {
            intervalSkips = 0;
            emit summaryChanged();
        }
    }
    if (worker)
        drain();
    bool repaint = false;
    if (!live() && play && !store.data.empty()) {
        auto end = store.data.back().elapsed + life;
        auto step = int64_t(dt * rate);
        playhead = step >= end - playhead ? end : playhead + step;
        visualTime = playhead;
        auto stop = afterTime(store.data, playhead);
        if (idleTimeout && (next != stop || visiblePeerExpired())) {
            layout();
            emit countsChanged();
        }
        schedule(next, stop);
        next = stop;
        if (playhead == end) {
            play = false;
            ended = true;
            emit stateChanged();
        }
        repaint = true;
        emit clockChanged();
    } else if (live() && play) {
        if (visiblePeerExpired()) {
            layout();
            emit countsChanged();
        }
        repaint = !scheduler.active.empty();
        scheduler.prune(visualTime);
        emit clockChanged();
    }
    if (solo) {
        repaint = true;
        if (now - soloStart >= life)
            solo.reset();
    }
    if (repaint)
        emit frame();
}
void Session::toggle() {
    if (stopping || busy() || (live() && !running()) || (!live() && store.data.empty()))
        return;
    if (ended)
        return restart();
    play = !play;
    lastTick = clock.nsecsElapsed();
    if (live() && play) {
        scheduler.active.clear();
        liveViewTime = lastTick;
        if (idleTimeout) {
            layout();
            emit countsChanged();
        }
        if (!store.data.empty())
            playhead = store.data.back().elapsed;
    }
    emit stateChanged();
    emit clockChanged();
    emit frame();
}
void Session::seek(double s) {
    if (live() || busy() || !std::isfinite(s) || store.data.empty())
        return;
    auto end = store.data.back().elapsed + life;
    playhead = s >= duration() ? end : int64_t(std::max(0., s) * 1e9);
    ended = playhead == end;
    if (ended)
        play = false;
    lastTick = clock.nsecsElapsed();
    if (idleTimeout)
        layout(true);
    reconstruct();
    emit clockChanged();
    emit stateChanged();
}
void Session::restart() {
    if (!live() && !busy() && !store.data.empty()) {
        seek(0);
        play = true;
        emit stateChanged();
    }
}
void Session::previewAt(double s) {
    play = false;
    seek(s);
}
void Session::setSpeed(double r) {
    if (r >= 0.25 && r <= 4) {
        rate = r;
        emit stateChanged();
    }
}
void Session::setReduced(bool v) {
    reduce = v;
    recentActivity.clear();
    QSettings().setValue("reduceMotion", v);
    scheduler.active.clear();
    solo.reset();
    emit stateChanged();
    emit frame();
}
void Session::selectRow(int i) {
    if (i >= 0 && size_t(i) < rows.size())
        selectPacket(rows[i]);
}
int Session::selectedRow() const {
    auto it = std::lower_bound(rows.begin(), rows.end(), selected);
    return it != rows.end() && *it == selected ? int(it - rows.begin()) : -1;
}
void Session::selectPacket(qulonglong id) {
    if (!store.get(id))
        return fail("This packet has expired from retained history.");
    selected = id;
    selectedPeer = {};
    inspect();
    layout();
    emit frame();
    emit inspectedPacket();
}
void Session::selectPeer(const QString &text) {
    selectedPeer = Address::parse(text.toStdString());
    selected = 0;
    inspect();
    layout();
    emit frame();
    emit inspectedPacket();
}
void Session::focusPeer(const QString &text) {
    auto a = Address::parse(text.toStdString());
    if (!a || !counts.contains(a))
        return;
    anchor = a;
    selected = 0;
    selectedPeer = a;
    layout(true);
    rebuildRows();
    inspect();
    if (live())
        scheduler.active.clear();
    else
        reconstruct();
    emit frame();
}
QString Session::peerLocation() const {
    if (!selectedPeer || !geoIp.enabled())
        return {};
    auto location = geoIp.lookup(selectedPeer).label();
    return location.isEmpty() ? QStringLiteral("Location unavailable") : location;
}
QVariantList Session::endpoints(const QString &query) const {
    QVariantList list;
    for (auto &[a, stats] : counts) {
        if (!matchesMapGroup(a))
            continue;
        auto addr = address(a), label = alias(a);
        if (!addr.contains(query, Qt::CaseInsensitive) && !label.contains(query, Qt::CaseInsensitive))
            continue;
        list << QVariantMap{{"address", addr},
                            {"label", label},
                            {"count", stats.packets()},
                            {"roles", endpointRoleLabel(stats.roles())},
                            {"location", geoIp.lookup(a).label()}};
        if (list.size() >= 250)
            break;
    }
    return list;
}
QString Session::protocolColor(QString n) const {
    for (auto p : {Protocol::TCP, Protocol::UDP, Protocol::DNS, Protocol::ICMP, Protocol::ARP})
        if (n == style(p).name)
            return style(p).color;
    return style(Protocol::Unknown).color;
}
QString Session::applyFilter(QString proto, QString peer, QString dir, QString lo, QString hi, QString expr) {
    Filter f, e;
    std::string error;
    if (!expression(expr.trimmed().toLower().toStdString(), e, error))
        return qs(error);
    if (proto != "All") {
        bool found = false;
        for (auto p :
             {Protocol::TCP, Protocol::UDP, Protocol::DNS, Protocol::ICMP, Protocol::ARP, Protocol::Unknown})
            if (proto == style(p).name) {
                f.proto = p;
                found = true;
            }
        if (!found)
            return "Choose a supported protocol.";
    }
    if (!peer.trimmed().isEmpty()) {
        auto a = Address::parse(peer.trimmed().toStdString());
        if (!a)
            return "Enter a valid IP or MAC address.";
        f.peer = a;
    }
    bool ok = true;
    if (!lo.isEmpty()) {
        f.min = lo.toUInt(&ok);
        if (!ok)
            return "Minimum size must be a positive integer or zero.";
    }
    if (!hi.isEmpty()) {
        f.max = hi.toUInt(&ok);
        if (!ok)
            return "Maximum size must be a positive integer or zero.";
    }
    if (f.max < f.min)
        return "Maximum size must be at least the minimum.";
    f.dir = dir == "Outgoing"   ? Direction::Out
            : dir == "Incoming" ? Direction::In
            : dir == "Other"    ? Direction::Other
                                : Direction::Any;
    filter = f;
    extra = e;
    QStringList parts;
    if (f.proto)
        parts << proto;
    if (f.peer)
        parts << peer;
    if (f.dir != Direction::Any)
        parts << dir;
    if (f.min)
        parts << "≥ " + lo + " B";
    if (f.max != UINT32_MAX)
        parts << "≤ " + hi + " B";
    if (!expr.trimmed().isEmpty())
        parts << expr;
    filterText = parts.join(" · ");
    rebuildRows();
    solo.reset();
    if (live())
        scheduler.active.clear();
    else
        reconstruct();
    emit filterChanged();
    emit frame();
    return {};
}
void Session::animateSelected() {
    auto *p = store.get(selected);
    if (!p || reduce || !accepts(*p))
        return;
    auto a = mapped(p->from), b = mapped(p->to);
    if (!a || !b)
        return fail("This packet has an unknown endpoint.");
    if (!mapAddresses.contains(a) || !mapAddresses.contains(b))
        return fail(idleTimeout ? "A packet endpoint is hidden. Check the idle timer and endpoint groups in Endpoints."
                                : "Choose All endpoints in Endpoints to animate this packet.");
    solo = Arrow{{p->id, a, b, p->proto, p->length(), 0}, int(Scheduler::routeBudget)};
    soloStart = clock.nsecsElapsed();
    emit frame();
}
void Session::inspect() {
    inspected.clear();
    auto *p = store.get(selected);
    if (!p) {
        emit selectionChanged();
        return;
    }
    Packet copy = *p;
    std::vector<Field> fields;
    decode(copy, &fields);
    QVariantList layers;
    QString layer;
    QVariantList items;
    auto finishLayer = [&] {
        if (!layer.isEmpty())
            layers << QVariantMap{{"name", layer}, {"fields", items}};
        items.clear();
    };
    for (auto &f : fields) {
        if (layer != qs(f.layer)) {
            finishLayer();
            layer = qs(f.layer);
        }
        items << QVariantMap{{"name", qs(f.name)},
                             {"value", qs(f.value)},
                             {"offset", int(f.offset)},
                             {"length", int(f.length)}};
    }
    finishLayer();
    auto sec = p->timestamp / 1000000000, frac = p->timestamp % 1000000000;
    if (frac < 0) {
        --sec;
        frac += 1000000000;
    }
    auto stamp = QDateTime::fromSecsSinceEpoch(sec, QTimeZone::UTC).toString("yyyy-MM-dd HH:mm:ss") + "." +
                 QString("%1").arg(frac, 9, 10, QChar('0')) + " UTC";
    auto dir = anchor && !locals.contains(anchor) ? direction(*p, {anchor}) : direction(*p, locals);
    QString d = dir == Direction::Out       ? "Outgoing"
                : dir == Direction::In      ? "Incoming"
                : locals.empty() && !anchor ? "Unassigned"
                                            : "Other";
    inspected = {{"id", number(p->id)},
                 {"protocol", protocol(*p)},
                 {"color", style(p->proto).color},
                 {"summary", qs(p->summary)},
                 {"status", qs(p->status)},
                 {"from", address(p->from)},
                 {"to", address(p->to)},
                 {"fromLabel", alias(p->from)},
                 {"toLabel", alias(p->to)},
                 {"fromPort", p->sport ? number(*p->sport) : ""},
                 {"toPort", p->dport ? number(*p->dport) : ""},
                 {"original", p->original ? number(p->original) : "Unavailable"},
                 {"captured", number(p->bytes.size())},
                 {"direction", d},
                 {"when", stamp},
                 {"elapsed", QString::number(double(p->elapsed) / 1e9, 'f', 3)},
                 {"layers", layers},
                 {"byteCount", int(p->bytes.size())}};
    inspected["size"] = QLocale(QLocale::English).toString(p->length()) + " B";
    emit selectionChanged();
}
void Session::rebuildBins() {
    bins.clear();
    std::array<int, 100> values{};
    if (!store.data.empty()) {
        auto range = std::max<int64_t>(1, store.data.back().elapsed + life);
        for (auto &p : store.data)
            ++values[std::min(99, int(double(p.elapsed) / range * 100))];
    }
    for (auto n : values)
        bins << n;
}
QVariantMap Session::totals() const {
    size_t mappedPeers = 0;
    for (auto a : slotsOnMap) {
        if (a == anchor && locals.contains(anchor)) {
            for (auto local : locals)
                if (counts.contains(local))
                    ++mappedPeers;
        } else if (counts.contains(a))
            ++mappedPeers;
    }
    return {{"delivered", number(kind == "Demo" ? store.data.size() : feed.delivered)},
            {"retained", number(store.data.size())},
            {"shown", number(rows.size())},
            {"evicted", number(store.evicted)},
            {"discarded", number(feed.discarded)},
            {"backend", feed.backendDrops ? number(*feed.backendDrops) : "Unavailable"},
            {"skipped", number(scheduler.skipped + hiddenSkipped)},
            {"endpoints", int(counts.size())},
            {"groupMatches", matchingAddresses()},
            {"mapPeers", int(slotsOnMap.size()) - int(mapAddresses.contains(anchor))},
            {"hidden", int(counts.size() - mappedPeers)},
            {"limited", feed.state == State::Limited}};
}
void Session::copy(const QString &s) { QGuiApplication::clipboard()->setText(s); }
void Session::exportCsv(const QUrl &url) {
    if (!url.isLocalFile())
        return fail("Select a local CSV destination.");
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());
    file.write("id,timestamp_ns,elapsed_ns,source,destination,source_port,destination_port,protocol,original_"
               "bytes,captured_bytes,summary,status\r\n");
    for (auto id : rows) {
        auto &p = *store.get(id);
        QStringList cells{number(id),
                          QString::number(p.timestamp),
                          QString::number(p.elapsed),
                          address(p.from),
                          address(p.to),
                          p.sport ? number(*p.sport) : "",
                          p.dport ? number(*p.dport) : "",
                          protocol(p),
                          p.original ? number(p.original) : "",
                          number(p.bytes.size()),
                          qs(p.summary),
                          qs(p.status)};
        std::string line;
        for (auto &cell : cells) {
            if (!line.empty())
                line += ',';
            line += csvCell(cell.toStdString());
        }
        line += "\r\n";
        if (file.write(line.data(), line.size()) != qint64(line.size()))
            return fail(file.errorString());
    }
    if (!file.commit())
        return fail(file.errorString());
    notice = "Exported " + number(rows.size()) + " filtered packets to " +
             QFileInfo(url.toLocalFile()).fileName() + ".";
    emit stateChanged();
}

QVariantMap Session::byteLine(int line) const {
    auto *p = store.get(selected);
    if (!p || line < 0 || size_t(line) >= ((p->bytes.size() + 7) / 8))
        return {};
    size_t start = size_t(line) * 8;
    QStringList hex;
    QString ascii;
    for (size_t i = start; i < std::min(start + 8, p->bytes.size()); ++i) {
        uint8_t c = p->bytes[i];
        hex << QString("%1").arg(c, 2, 16, QChar('0'));
        ascii += c >= 32 && c < 127 ? QChar(c) : QChar('.');
    }
    return {{"offset", QString("%1").arg(start, 4, 16, QChar('0'))}, {"hex", hex}, {"ascii", ascii}};
}
