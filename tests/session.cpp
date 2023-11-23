#include "fixtures.hpp"
#include "map.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace pf;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) +                \
                                     " failed: " + #x);                                                      \
    } while (false)
namespace {
QStringList warnings;
struct Script {
    Feed feed;
    int opens = 0, closed = 0, stops = 0;
};
struct FakeSource : Source {
    Script &script;
    explicit FakeSource(Script &s) : script(s) {
        ++s.opens;
        s.feed = {};
        s.feed.state = State::Running;
    }
    ~FakeSource() override { ++script.closed; }
    void requestStop() override {
        ++script.stops;
        script.feed.state = State::Stopped;
    }
    Feed take() override {
        auto result = script.feed;
        script.feed.packets.clear();
        return result;
    }
};
struct Backend : CaptureBackend {
    Script &script;
    explicit Backend(Script &s) : script(s) {}
    std::vector<Interface> devices(std::string &) override {
        return {{"synthetic", "Test interface", {Address::parse("192.0.2.24")}}};
    }
    std::unique_ptr<Source> open(const std::string &path, bool live) override {
        if (live)
            return std::make_unique<FakeSource>(script);
        return CaptureBackend::open(path, false);
    }
};
void wait(std::function<bool()> condition) {
    for (int n = 0; n < 200 && !condition(); ++n)
        QTest::qWait(10);
    CHECK(condition());
}
struct Pixels {
    size_t count = 0;
    QPoint top, bottom, middle;
};
Pixels ink(QQuickWindow *w, PacketMap *map, QColor target) {
    QTest::qWait(45);
    auto shot = w->grabWindow();
    CHECK(!shot.isNull());
    auto img = shot.scaled(w->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPoint start = map->mapToScene({0, 0}).toPoint();
    Pixels pixels;
    for (int y = start.y() + 50; y < start.y() + map->height() * .85; ++y)
        for (int x = start.x() + map->width() * .23; x < start.x() + map->width() * .80; ++x) {
            auto c = img.pixelColor(x, y);
            if (std::abs(c.red() - target.red()) + std::abs(c.green() - target.green()) +
                    std::abs(c.blue() - target.blue()) >
                14)
                continue;
            if (!pixels.count)
                pixels.top = {x, y};
            pixels.bottom = {x, y};
            if (pixels.count % 31 == 0)
                pixels.middle = {x, y};
            ++pixels.count;
        }
    return pixels;
}
void visual(Session &s, QQuickWindow *window) {
    auto *map = window->findChild<PacketMap *>();
    CHECK(map);
    s.previewAt(.285);
    CHECK(s.applyFilter("TCP", "", "Any", "", "", "").isEmpty());
    s.selectPacket(2);
    CHECK(s.arrows().size() == 1 && s.arrows()[0].arrow.id == 1);
    auto amber = QColor(style(Protocol::TCP).color);
    auto half = ink(window, map, amber);
    CHECK(half.count > 100);
    s.previewAt(.59);
    auto full = ink(window, map, amber);
    CHECK(full.count > half.count * 1.6 && full.top.y() < half.top.y() - 35 &&
          std::abs(full.bottom.y() - half.bottom.y()) < 3);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, full.middle);
    CHECK(s.details()["id"].toString() == "1");
    s.previewAt(.74);
    CHECK(s.arrows().size() == 1 && s.arrows()[0].phase.opacity < .6);
    s.previewAt(.90);
    CHECK(ink(window, map, amber).count == 0 && s.arrows().empty());
    CHECK(s.applyFilter("UDP", "", "Any", "", "", "").isEmpty());
    auto cyan = QColor(style(Protocol::UDP).color);
    s.previewAt(.55);
    auto incoming = ink(window, map, cyan);
    s.previewAt(.90);
    auto arrived = ink(window, map, cyan);
    CHECK(incoming.count > 30 && arrived.bottom.y() > incoming.bottom.y() + 30 &&
          std::abs(incoming.top.y() - arrived.top.y()) < 3);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, arrived.middle);
    CHECK(s.details()["id"].toString() == "3");
    CHECK(s.applyFilter("All", "", "Any", "", "", "").isEmpty());
    window->setProperty("showMap", false);
    QTest::qWait(50);
    s.selectRow(1);
    CHECK(s.details()["id"].toString() == "2" && s.selectedRow() == 1);
    QTest::keyClick(window, Qt::Key_Right);
    CHECK(s.details()["id"].toString() == "3");
    window->setProperty("showMap", true);
}
void inspectionUi(Session &s, QQuickWindow *window) {
    auto *sheets = window->findChild<QObject *>("sheets");
    auto *panel = window->findChild<QObject *>("inspector");
    CHECK(sheets && panel);
    for (auto [method, name] :
         {std::pair{"showFilter", "filters"}, {"endpoints", "endpoints"}, {"stats", "stats"}}) {
        CHECK(QMetaObject::invokeMethod(sheets, method));
        QTest::qWait(70);
        auto *dialog = window->findChild<QObject *>(name);
        CHECK(dialog && dialog->property("visible").toBool() && dialog->property("height").toDouble() > 180);
        CHECK(QMetaObject::invokeMethod(dialog, "close"));
        QTest::qWait(50);
    }
    s.selectPacket(2);
    panel->setProperty("tab", 1);
    panel->setProperty("openLayer", "DNS");
    QTest::qWait(40);
    CHECK(QMetaObject::invokeMethod(panel, "showBytes", Q_ARG(QVariant, 54), Q_ARG(QVariant, 14)));
    QTest::qWait(50);
    CHECK(panel->property("tab") == 2 && panel->property("byteStart") == 54);
    panel->setProperty("tab", 0);
}
void inspectReplay(Session &s, QTemporaryDir &temp) {
    s.tryDemo();
    s.previewAt(3.6);
    auto time = s.position();
    auto n = s.totals()["retained"];
    s.selectPacket(10);
    s.animateSelected();
    auto arrows = s.arrows();
    CHECK(std::count_if(arrows.begin(), arrows.end(), [](auto &a) { return a.arrow.id == 10; }) == 1);
    CHECK(s.position() == time && s.totals()["retained"] == n);
    CHECK(s.byteLine(0)["hex"].toStringList().size() == 8);
    auto error = s.applyFilter("All", "", "Any", "", "", "protocol = dns");
    CHECK(error.isEmpty());
    auto count = s.rowCount();
    CHECK(count > 0 && count < 224);
    CHECK(!s.applyFilter("All", "", "Any", "", "", "broken").isEmpty() && s.rowCount() == count);
    s.selectRow(0);
    CHECK(s.details()["protocol"] == "DNS");
    auto csv = temp.filePath("filtered.csv");
    s.exportCsv(QUrl::fromLocalFile(csv));
    QFile f(csv);
    CHECK(f.open(QIODevice::ReadOnly));
    auto content = f.readAll();
    CHECK(content.count('\n') == count + 1 && content.contains("example.test"));
    auto path = temp.filePath("file.pcapng");
    fixtures::write(path.toStdString(), fixtures::pcapng(demo()));
    s.openFile(QUrl::fromLocalFile(path));
    wait([&] { return !s.busy(); });
    CHECK(s.rowCount() == 224 && !s.playing() && s.focus().isEmpty());
    CHECK(s.details()["direction"] == "Unassigned" && s.details()["size"] == "1,248 B");
    s.setMapGroups(Session::WebServers);
    CHECK(s.nodes().size() == 1 && s.nodes()[0].addressText == "203.0.113.42" && s.rowCount() == 224);
    CHECK(s.nodes().front().subtitle(false) == "Likely web server");
    s.setMapGroups(Session::AllGroups);
    s.focusPeer("203.0.113.42");
    s.selectPacket(1);
    CHECK(s.details()["direction"] == "Incoming" && s.details()["toLabel"] == "203.0.113.42");
    s.seek(s.duration());
    CHECK(s.finished() && !s.playing());
    s.restart();
    CHECK(s.playing() && !s.finished());
    s.previewAt(.35);
    s.setReduced(true);
    CHECK(s.arrows().empty() && s.rowCount() == 224);
    s.setReduced(false);
}
void live(Session &s, Script &script) {
    s.startLive("missing");
    CHECK(script.opens == 0);
    s.listInterfaces();
    auto data = demo();
    s.startLive("synthetic");
    wait([&] { return s.status() == "Capturing"; });
    auto publish = [&](int n) {
        auto &f = script.feed;
        for (int i = 0; i < n; ++i) {
            auto p = data[f.delivered % data.size()];
            p.id = ++f.delivered;
            p.elapsed = p.id * 1000000;
            f.packets.push_back(std::move(p));
        }
        wait([&] { return f.packets.empty(); });
    };
    publish(8);
    s.selectPacket(1);
    s.toggle();
    auto paused = s.position();
    auto old = s.arrows();
    publish(150);
    CHECK(!s.playing() && s.position() == paused && s.rowCount() == 158 && old.size() == s.arrows().size());
    s.toggle();
    CHECK(s.playing() && s.position() > paused && s.arrows().empty());
    script.feed.discarded = 5;
    script.feed.backendDrops = 9;
    publish(512);
    CHECK(s.totals()["skipped"].toUInt() > 0 && s.details()["id"] == "1" && s.totals()["backend"] == "9" &&
          s.totals()["discarded"] == "5");
    for (int i = 0; i < 48; ++i)
        publish(512);
    CHECK(s.rowCount() == 24000 && s.totals()["evicted"].toUInt() > 0 && !s.details().contains("id"));
    s.stopSource();
    CHECK(!s.running() && script.closed == 1 && script.stops == 1);
    s.startLive("synthetic");
    publish(5);
    CHECK(s.rowCount() == 5);
    script.feed.state = State::Error;
    script.feed.message = "Interface removed";
    wait([&] { return !s.running(); });
    CHECK(s.message() == "Interface removed");
    s.startLive("synthetic");
    publish(2);
    s.tryDemo();
    CHECK(s.rowCount() == 224 && script.closed == 3);
}
QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems())
        if (auto *found = visualItem(child, name))
            return found;
    return nullptr;
}
// Find flat-color card or icon backgrounds in the actual rendered viewport.
std::vector<QRect> renderedRects(QQuickWindow *window, PacketMap *map, QColor target, QSizeF worldSize) {
    QTest::qWait(60);
    auto image = window->grabWindow().scaled(window->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QRect area(map->mapToScene({0, 0}).toPoint(), map->size().toSize());
    auto crop = image.copy(area);
    const int w = crop.width(), h = crop.height();
    std::vector<bool> match(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto c = crop.pixelColor(x, y);
            match[y * w + x] = std::abs(c.red() - target.red()) + std::abs(c.green() - target.green()) +
                                   std::abs(c.blue() - target.blue()) <
                               5;
        }
    std::vector<QRect> regions;
    const auto expected = worldSize * (map->zoomPercent() / 100.0);
    for (int i = 0; i < w * h; ++i) {
        if (!match[i])
            continue;
        std::vector<int> component{i};
        QRect bounds(QPoint(i % w, i / w), QSize(1, 1));
        match[i] = false;
        for (size_t j = 0; j < component.size(); ++j) {
            int k = component[j];
            bounds |= QRect(QPoint(k % w, k / w), QSize(1, 1));
            for (int neighbor : {k - 1, k + 1, k - w, k + w}) {
                if (neighbor < 0 || neighbor >= w * h || !match[neighbor] ||
                    std::abs(neighbor % w - k % w) > 1)
                    continue;
                match[neighbor] = false;
                component.push_back(neighbor);
            }
        }
        if (std::abs(bounds.width() - expected.width()) <= 4 &&
            std::abs(bounds.height() - expected.height()) <= 4)
            regions.push_back(bounds.translated(area.topLeft()));
    }
    return regions;
}
void mapControls(Session &s, QQuickWindow *window) {
    auto *map = window->findChild<PacketMap *>();
    auto *sheets = window->findChild<QObject *>("sheets");
    CHECK(map && sheets);
    s.tryDemo();
    s.previewAt(.74);
    auto point = [&](double x, double y) {
        return map->mapToScene({x * map->width(), y * map->height()}).toPoint();
    };
    auto click = [&](QPoint where) { QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, where); };
    auto dnsIcons = renderedRects(window, map, QColor("#f0eafb"), {32, 32});
    auto webIcons = renderedRects(window, map, QColor("#e5f3fc"), {32, 32});
    CHECK(!dnsIcons.empty() && !webIcons.empty());
    const auto dns = dnsIcons.front().center(), web = webIcons.front().center(), moved = point(.15, .52);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dns);
    QTest::mouseMove(window, moved);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, moved);
    click(moved);
    CHECK(s.peer() == "192.0.2.53");
    click(web);
    CHECK(s.peer() == "203.0.113.42");
    click(dns);
    CHECK(s.peer() == "203.0.113.42");

    // Use the actual controls to isolate a role, add a second role, and type a custom cap.
    CHECK(QMetaObject::invokeMethod(sheets, "endpoints"));
    QTest::qWait(80);
    auto button = [&](QString name) {
        auto *item = visualItem(window->contentItem(), name);
        CHECK(item);
        click(item->mapToScene(item->boundingRect().center()).toPoint());
    };
    button("group1");
    CHECK(s.mapGroups() == Session::WebServers && s.nodes().size() == 2);
    CHECK(s.endpoints("").size() == 1 && s.endpoints("").front().toMap()["address"] == "203.0.113.42");
    CHECK(s.rowCount() == 224);
    button("group2");
    CHECK(s.mapGroups() == (Session::WebServers | Session::DnsServers) && s.nodes().size() == 3);
    button("group7");
    CHECK(s.mapGroups() == Session::AllGroups);
    button("peerLimit");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_7);
    QTest::keyClick(window, Qt::Key_3);
    QTest::keyClick(window, Qt::Key_Return);
    CHECK(s.peerLimit() == 73 && QSettings().value("mapPeerLimit").toInt() == 73);
    CHECK(QMetaObject::invokeMethod(window->findChild<QObject *>("endpoints"), "close"));
    QTest::qWait(50);
    click(moved);
    CHECK(s.peer() == "192.0.2.53");
    s.selectPacket(2);
    CHECK(s.applyFilter("DNS", "", "Any", "", "", "").isEmpty());
    s.previewAt(.75);
    auto purple = ink(window, map, QColor(style(Protocol::DNS).color));
    // The incoming edge of the dragged resolver is now near its new center.
    CHECK(purple.count > 10 && purple.bottom.y() > dns.y() + 35 && std::abs(purple.bottom.y() - moved.y()) < 35);
    s.setPeerLimit(4);
    map->resetLayout();
    click(dns);
    CHECK(s.peer() == "192.0.2.53");
    click(web);
    click(moved);
    CHECK(s.peer() == "203.0.113.42");
    // The same drag uses world coordinates when zoomed. The background remains independently pannable.
    map->scaleBy(1.25);
    auto origin = point(.5, .5);
    auto scaledWeb = origin + (web - origin) * 1.25;
    auto drop = scaledWeb + QPoint(-65, 92);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, scaledWeb);
    QTest::mouseMove(window, drop);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, drop);
    click(origin + (dns - origin) * 1.25);
    CHECK(s.peer() == "192.0.2.53");
    click(drop);
    CHECK(s.peer() == "203.0.113.42");
    auto panStart = point(.90, .62);
    QPoint shift(-42, 24);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, panStart);
    QTest::mouseMove(window, panStart + shift);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, panStart + shift);
    click(origin + (dns - origin) * 1.25 + shift);
    CHECK(s.peer() == "192.0.2.53");
    click(drop + shift);
    CHECK(s.peer() == "203.0.113.42");
    map->resetLayout();
    s.setPeerLimit(-10);
    CHECK(s.peerLimit() == 4);
}

void stablePeerPositions(Session &s, QQuickWindow *window) {
    auto *map = window->findChild<PacketMap *>();
    CHECK(map);
    s.setIdleSeconds(1);
    s.tryDemo();
    s.previewAt(0);
    auto icon = [&](QColor color) {
        auto matches = renderedRects(window, map, color, {32, 32});
        CHECK(matches.size() == 1);
        return matches.front();
    };
    const QColor webColor("#e5f3fc"), dnsColor("#f0eafb"), localColor("#e7eefb");
    // Fit once with only the web peer visible. Traffic and timeouts must
    // preserve this camera as well as the world positions of surviving peers.
    map->fit();
    QSignalSpy camera(map, &PacketMap::viewChanged);
    const auto web = icon(webColor), local = icon(localColor);
    s.previewAt(.15);
    CHECK(icon(webColor) == web);
    const auto dns = icon(dnsColor);
    s.previewAt(1.5); // Web has expired, the gateway joins, DNS is still active.
    CHECK(icon(dnsColor) == dns && icon(localColor) == local);
    s.previewAt(4.8); // Only the focus remains.
    CHECK(s.nodes().size() == 1 && icon(localColor) == local);
    s.previewAt(.15);
    CHECK(icon(webColor) == web && icon(dnsColor) == dns);

    s.setMapGroups(Session::DnsServers);
    CHECK(icon(dnsColor) == dns);
    s.setMapGroups(Session::AllGroups);
    CHECK(icon(webColor) == web && camera.isEmpty());
    s.setIdleSeconds(0);
}

void checkCrowdedRings(QQuickWindow *window, PacketMap *map) {
    const auto peers = renderedRects(window, map, Qt::white, {159, 58});
    CHECK(peers.size() == 26);
    QPointF center;
    for (size_t i = 0; i < peers.size(); ++i) {
        center += peers[i].center();
        for (size_t j = 0; j < i; ++j)
            CHECK(!peers[i].intersects(peers[j]));
    }
    center /= peers.size();
    const auto local = renderedRects(window, map, Qt::white, {184, 66});
    CHECK(local.size() == 1 && QLineF(center, local.front().center()).length() < 2);

    // Measure the two axes from the rendered peer positions. Equal angular
    // steps on each ring give equal variance after removing the aspect ratio.
    double xx = 0, yy = 0;
    for (auto p : peers) {
        const auto d = QPointF(p.center()) - center;
        xx += d.x() * d.x();
        yy += d.y() * d.y();
    }
    const double aspect = std::sqrt(xx / yy);
    std::vector<std::pair<double, double>> polar;
    for (auto p : peers) {
        const auto d = QPointF(p.center()) - center;
        polar.emplace_back(std::hypot(d.x() / aspect, d.y()), std::atan2(d.y(), d.x() / aspect));
    }
    std::sort(polar.begin(), polar.end());
    for (size_t i = 0; i < polar.size();) {
        const double radius = polar[i].first;
        std::vector<double> angles;
        while (i < polar.size() && polar[i].first - radius < 3)
            angles.push_back(polar[i++].second);
        CHECK(angles.size() >= 3);
        std::sort(angles.begin(), angles.end());
        const double step = 2 * std::numbers::pi / angles.size();
        angles.push_back(angles.front() + 2 * std::numbers::pi);
        for (size_t j = 1; j < angles.size(); ++j)
            CHECK(std::abs(angles[j] - angles[j - 1] - step) < .05);
    }
}
void manyPeers(Session &s, Script &script, QQuickWindow *window) {
    auto *map = window->findChild<PacketMap *>();
    CHECK(map);
    s.listInterfaces();
    s.startLive("synthetic");
    auto records = demo();
    auto publish = [&](std::vector<Packet> batch) {
        for (auto &p : batch) {
            p.id = ++script.feed.delivered;
            p.elapsed = p.id * ms;
        }
        script.feed.packets = std::move(batch);
        wait([&] { return script.feed.packets.empty(); });
    };
    auto remote = [](int i) { return Address::parse("198.51.100." + std::to_string(i)); };
    std::vector<Packet> batch;
    for (int i = 1; i <= 112; ++i) {
        auto p = records[i <= 75 ? 0 : i <= 95 ? 1 : 4];
        p.to = remote(i);
        batch.push_back(p);
    }
    // Include return traffic from a dual-role server and a UDP/443 service,
    // plus undecoded port-53 traffic, which is insufficient DNS evidence.
    auto dns = records[3];
    dns.from = remote(1);
    batch.push_back(dns);
    auto udp = records[2];
    udp.from = remote(96);
    batch.push_back(udp);
    udp.sport = 53;
    udp.from = remote(97);
    batch.push_back(udp);
    auto http = records[0];
    http.from = remote(98);
    http.to = records[0].from;
    http.sport = 80;
    http.dport = 52000;
    batch.push_back(http);
    publish(std::move(batch));
    CHECK(s.nodes().size() == 5 && s.totals()["hidden"].toInt() == 108);
    s.setPeerLimit(26);
    checkCrowdedRings(window, map);
    s.setPeerLimit(1000);
    CHECK(s.peerLimit() == 100 && s.nodes().size() == 101 && s.totals()["hidden"].toInt() == 12);
    auto subtitle = [&](Address addr) {
        for (const auto &node : s.nodes())
            if (node.address == addr)
                return node.subtitle(false);
        return QString{};
    };
    CHECK(subtitle(remote(1)) == "DNS + likely web");
    CHECK(subtitle(remote(76)) == "DNS server");
    CHECK(subtitle(remote(96)) == "Likely web server");
    CHECK(subtitle(remote(97)) == "Role unknown");
    CHECK(subtitle(records[0].from) == "192.0.2.24");
    s.toggle();
    CHECK(!s.playing());
    map->fit();
    CHECK(map->zoomPercent() < 100);
    const auto peers = renderedRects(window, map, Qt::white, {159, 58});
    std::map<QString, QPoint> centers;
    for (auto rect : peers) {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, rect.center());
        if (s.peer().startsWith("198.51.100."))
            centers.emplace(s.peer(), rect.center());
    }
    CHECK(centers.size() == 100);
    QSignalSpy camera(map, &PacketMap::viewChanged);
    s.setMapGroups(Session::WebServers);
    CHECK(s.nodes().size() == 78 && s.endpoints("").size() == 77);
    auto total = s.rowCount();
    s.selectPacket(97);
    CHECK(s.nodes().size() == 78);
    CHECK(s.endpoints("198.51.100.97").isEmpty());
    s.setMapGroups(Session::DnsServers);
    CHECK(s.nodes().size() == 22 && s.endpoints("").size() == 21);
    s.setMapGroups(Session::WebServers | Session::DnsServers);
    CHECK(s.nodes().size() == 98 && s.totals()["groupMatches"].toInt() == 97 && s.rowCount() == total);
    // New matching endpoints join during capture. A group also loses its evidence after retention eviction.
    auto discovered = records[1];
    discovered.to = remote(112);
    publish({discovered});
    CHECK(s.nodes().size() == 99 && s.endpoints("198.51.100.112").size() == 1);
    // All slots had owners. The new peer reuses a hidden peer's slot, and
    // every surviving card remains clickable at its original screen position.
    for (const auto &node : s.nodes()) {
        if (node.focus || node.address == remote(112))
            continue;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centers.at(node.addressText));
        CHECK(s.peer() == node.addressText);
    }
    CHECK(camera.isEmpty());
    s.setPeerLimit(4);
    CHECK(s.nodes().size() == 5);
    auto ordinary = records[4];
    ordinary.to = remote(1);
    for (int i = 0; i < 24; ++i)
        publish(std::vector<Packet>(1000, ordinary));
    CHECK(s.totals()["groupMatches"].toInt() == 0 && s.nodes().size() == 1 && s.endpoints("").isEmpty());
    s.setMapGroups(Session::OtherPeers);
    CHECK(s.endpoints("").size() == 2);
    CHECK(subtitle(remote(1)) == "Role unknown");
    s.stopSource();
    s.tryDemo();
    CHECK(s.mapGroups() == Session::AllGroups && s.peerLimit() == 4);
}
void idleTimer(Session &s, Script &script, QQuickWindow *window) {
    s.tryDemo();
    s.previewAt(0);
    CHECK(s.idleSeconds() == 0 && s.nodes().size() == 5);
    CHECK(QMetaObject::invokeMethod(window->findChild<QObject *>("sheets"), "endpoints"));
    QTest::qWait(80);
    auto *slider = visualItem(window->contentItem(), "idleTimeoutSlider");
    CHECK(slider);
    auto point = [&](double x) {
        return slider->mapToScene({slider->width() * x, slider->height() / 2}).toPoint();
    };
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point(0.02));
    QTest::mouseMove(window, point(0.55));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point(0.55));
    CHECK(s.idleSeconds() >= 60 && s.idleSeconds() <= 75);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point(0.01));
    CHECK(s.idleSeconds() == 0);
    slider->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Right);
    CHECK(s.idleSeconds() == 1 && QSettings().value("mapIdleSeconds").toInt() == 1);
    CHECK(QMetaObject::invokeMethod(window->findChild<QObject *>("endpoints"), "close"));

    const auto dns = Address::parse("192.0.2.53"), web = Address::parse("203.0.113.42");
    auto visible = [&](Address a) {
        auto nodes = s.nodes();
        return std::any_of(nodes.begin(), nodes.end(), [&](const auto &node) { return node.address == a; });
    };
    CHECK(s.nodes().size() == 2 && visible(web) && !visible(dns));
    s.previewAt(.15); // DNS received a query.
    CHECK(visible(dns));
    s.previewAt(2.12); // DNS sent its reply at 1.15 seconds.
    CHECK(visible(dns) && !visible(web));
    s.setSpeed(4);
    s.toggle();
    wait([&] { return !visible(dns); }); // Expires between packets.
    s.previewAt(2.15);
    CHECK(!visible(dns));
    s.selectPeer("192.0.2.53");
    CHECK(!visible(dns) && s.endpoints("192.0.2.53").size() == 1 && s.rowCount() == 224);
    s.previewAt(1.0);
    CHECK(visible(dns) && visible(web));
    s.setMapGroups(Session::DnsServers);
    CHECK(visible(dns) && !visible(web));
    s.previewAt(5);
    CHECK(s.nodes().size() == 1);
    s.setSpeed(1);

    s.listInterfaces();
    s.startLive("synthetic");
    auto publish = [&](Packet packet) {
        packet.id = ++script.feed.delivered;
        script.feed.packets.push_back(std::move(packet));
        wait([&] { return script.feed.packets.empty(); });
    };
    auto records = demo();
    publish(records[0]); // Local to web.
    CHECK(visible(web));
    s.toggle();
    QTest::qWait(1050);
    publish(records[3]); // DNS to local while the view is paused.
    CHECK(visible(web) && !visible(dns));
    s.toggle();
    CHECK(!visible(web) && visible(dns));
    wait([&] { return !visible(dns); });
    CHECK(s.nodes().size() == 1 && s.rowCount() == 2);
    publish(records[2]); // Web to local brings the expired web peer back.
    CHECK(visible(web));
    s.setIdleSeconds(0);
    CHECK(visible(dns) && visible(web));
    s.stopSource();
    s.tryDemo();
}
void ipLocations(Session &s, Script &script, QQuickWindow *window) {
    auto *geo = s.geo();
    auto *sheets = window->findChild<QObject *>("sheets");
    CHECK(sheets && !geo->enabled());
    // The synthetic MMDB has a city record for IPv4 and a country-only record for 2xxx IPv6.
    const auto fixture = QFINDTESTDATA("fixtures/locations.mmdb");
    CHECK(!fixture.isEmpty() && geo->chooseDatabase(QUrl::fromLocalFile(fixture)));
    auto label = [&](const char *ip) { return geo->lookup(Address::parse(ip)).label(); };
    const QString city = QStringLiteral("🇺🇸 Example City, United States");
    CHECK(label("8.8.8.8") == city && label("::ffff:8.8.8.8") == city);
    CHECK(label("2606:4700:4700::1111") == QStringLiteral("🇩🇪 Germany"));
    for (auto ip : {"192.168.0.1", "127.0.0.1", "::ffff:192.168.0.1", "203.0.113.1", "2001:db8::1",
                    "fd00::1", "fe80::1", "ff02::1", "3000::1"})
        CHECK(label(ip).isEmpty());
    CHECK(!geo->chooseDatabase(QUrl::fromLocalFile(fixture + ".missing")));
    CHECK(!geo->error().isEmpty() && label("8.8.8.8") == city && GeoIp().enabled());

    s.listInterfaces();
    s.startLive("synthetic");
    auto p = demo().front();
    p.to = Address::parse("8.8.8.8");
    p.id = ++script.feed.delivered;
    script.feed.packets.push_back(p);
    wait([&] { return s.rowCount() == 1; });
    s.selectPeer("8.8.8.8");
    CHECK(s.peerLocation() == city && s.endpoints("8.8.8.8").at(0).toMap()["location"] == city);
    auto nodes = s.nodes();
    CHECK(std::any_of(nodes.begin(), nodes.end(), [&](const auto &n) {
        return n.address == p.to && n.location.label() == city;
    }));
    CHECK(QMetaObject::invokeMethod(sheets, "endpoints"));
    QTest::qWait(80);
    auto *toggle = visualItem(window->contentItem(), "endpointLocationsToggle");
    CHECK(toggle && toggle->property("checked").toBool());
    auto clickToggle = [&] {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          toggle->mapToScene(toggle->boundingRect().center()).toPoint());
    };
    clickToggle();
    CHECK(!geo->enabled() && !GeoIp().enabled() && s.peerLocation().isEmpty());
    CHECK(s.endpoints("8.8.8.8").at(0).toMap()["location"].toString().isEmpty());
    clickToggle();
    CHECK(geo->enabled() && geo->error().isEmpty() && s.peerLocation() == city);
    CHECK(QMetaObject::invokeMethod(window->findChild<QObject *>("endpoints"), "close"));
    s.selectPeer("192.0.2.24");
    CHECK(s.peerLocation() == "Location unavailable");
    geo->setEnabled(false);
    s.stopSource();
    s.tryDemo();
}
void burst(Session &s, Script &script, QQuickWindow *window) {
    s.listInterfaces();
    s.startLive("synthetic");
    auto d = demo();
    std::vector<double> intervals;
    QElapsedTimer frame, run;
    run.start();
    auto connection = QObject::connect(
        window, &QQuickWindow::frameSwapped, window,
        [&] {
            if (frame.isValid())
                intervals.push_back(frame.nsecsElapsed() / 1e6);
            frame.start();
        },
        Qt::QueuedConnection);
    int batches = 0;
    while (run.elapsed() < 6000) {
        auto &f = script.feed;
        if (f.packets.empty()) {
            for (int i = 0; i < 384; ++i) {
                auto p = d[f.delivered % d.size()];
                p.id = ++f.delivered;
                p.elapsed = p.id * 40000;
                f.packets.push_back(std::move(p));
            }
            if (++batches % 20 == 0 && s.rowCount())
                s.selectRow(s.rowCount() - 1);
        }
        QTest::qWait(4);
    }
    QObject::disconnect(connection);
    s.stopSource();
    std::sort(intervals.begin(), intervals.end());
    CHECK(!intervals.empty() && s.totals()["retained"].toInt() == 24000);
    std::cout << "Synthetic GUI live source: " << s.totals()["delivered"].toString().toStdString()
              << " packets in " << run.elapsed() << " ms; " << s.totals()["evicted"].toString().toStdString()
              << " evicted; " << s.totals()["skipped"].toString().toStdString() << " animations skipped; "
              << intervals.size() << " frames, median " << intervals[intervals.size() / 2] << " ms, p95 "
              << intervals[intervals.size() * 95 / 100] << " ms\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        QGuiApplication app(argc, argv);
        QTemporaryDir temp;
        CHECK(temp.isValid());
        app.setOrganizationName("PacketFlowTests");
        app.setApplicationName("Packet Flow tests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        QQuickStyle::setStyle("Basic");
        qmlRegisterType<PacketMap>("PacketFlow", 1, 0, "PacketMap");
        qmlRegisterUncreatableType<Session>("PacketFlow", 1, 0, "PacketSession", "Test session");
        Script script;
        Session s(nullptr, std::make_unique<Backend>(script));
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](auto errors) {
            for (auto &e : errors)
                warnings << e.toString();
        });
        engine.rootContext()->setContextProperty("flow", &s);
        engine.load(QUrl("qrc:/ui/Main.qml"));
        CHECK(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        CHECK(window);
        QTest::qWait(100);
        if (app.arguments().contains("--burst")) {
            burst(s, script, window);
            return 0;
        }
        visual(s, window);
        inspectionUi(s, window);
        inspectReplay(s, temp);
        live(s, script);
        mapControls(s, window);
        stablePeerPositions(s, window);
        manyPeers(s, script, window);
        idleTimer(s, script, window);
        ipLocations(s, script, window);
        CHECK(warnings.empty());
        std::cout << "Packet inspection, replay, live lifecycle, map dragging, role groups and 100 "
                     "selectable peers passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
