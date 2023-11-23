#include "capture.hpp"
#include "fixtures.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <pcap/dlt.h>
#include <stdexcept>

using namespace pf;
using namespace std::chrono_literals;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) +                \
                                     " failed: " + #x);                                                      \
    } while (false)
namespace {
struct Temp {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("packet-flow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directory(path); }
    ~Temp() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
void decoder() {
    auto d = demo();
    CHECK(d.size() == 224);
    CHECK(d[0].from.text() == "192.0.2.24");
    CHECK(d[0].to.text() == "203.0.113.42");
    CHECK(d[0].original == 1248 && d[0].proto == Protocol::TCP);
    CHECK(d[1].proto == Protocol::DNS && d[1].summary == "Query · example.test");
    CHECK(d[3].summary == "Response · example.test");
    CHECK(d[2].proto == Protocol::UDP);
    CHECK(d[6].proto == Protocol::ICMP && d[6].to.text() == "2001:db8::8");
    for (auto p : {d[0], d[1], d[6]}) {
        auto whole = p.bytes;
        for (size_t n = 0; n <= whole.size(); ++n) {
            p.bytes.assign(whole.begin(), whole.begin() + n);
            std::vector<Field> fields;
            decode(p, &fields);
            for (auto &f : fields)
                CHECK(f.offset <= n && f.length <= n - f.offset);
        }
    }
    auto p = d[1];
    p.bytes[54] = 0xc0;
    p.bytes[55] = 12;
    decode(p);
    CHECK(p.proto == Protocol::UDP);
    p = d[1];
    p.bytes.resize(50);
    decode(p);
    CHECK(p.proto != Protocol::DNS && p.status != "Decoded");
    p = d[1];
    p.bytes[20] = 0;
    p.bytes[21] = 1;
    decode(p);
    CHECK(!p.sport && p.status == "Non-initial IPv4 fragment");
    p = d[1];
    p.bytes[20] = 0x20;
    decode(p);
    CHECK(p.proto == Protocol::UDP && p.status == "First fragment");
    p = d[0];
    p.bytes[46] = 0x10;
    decode(p);
    CHECK(p.status == "Invalid TCP data offset");
    p = d[6];
    p.bytes.insert(p.bytes.begin() + 54, {58, 0, 0, 8, 0, 0, 0, 1});
    p.bytes[20] = 44;
    p.bytes[19] += 8;
    decode(p);
    CHECK(!p.sport && p.status == "Non-initial IPv6 fragment");
    p.bytes[57] = 0;
    decode(p);
    CHECK(p.proto == Protocol::ICMP);
    p = d[6];
    p.bytes.insert(p.bytes.begin() + 54, {58, 0, 0, 0, 0, 0, 0, 0});
    p.bytes[20] = 0;
    p.bytes[19] += 8;
    decode(p);
    CHECK(p.proto == Protocol::ICMP);
    p = d[1];
    p.bytes.insert(p.bytes.begin() + 12, {0x81, 0, 0, 37});
    decode(p);
    CHECK(p.proto == Protocol::DNS);
    for (int i = 0; i < 4; ++i)
        p.bytes.insert(p.bytes.begin() + 12, {0x81, 0, 0, 37});
    decode(p);
    CHECK(p.status == "VLAN nesting limit");
    for (int link : {DLT_NULL, DLT_LOOP, DLT_RAW}) {
        p = d[0];
        p.bytes.erase(p.bytes.begin(), p.bytes.begin() + 14);
        p.link = link;
        if (link == DLT_NULL)
            p.bytes.insert(p.bytes.begin(), {2, 0, 0, 0});
        if (link == DLT_LOOP)
            p.bytes.insert(p.bytes.begin(), {0, 0, 0, 2});
        decode(p);
        CHECK(p.proto == Protocol::TCP && p.dport == 443);
    }
    p = d[0];
    p.link = 9876;
    decode(p);
    CHECK(p.proto == Protocol::Unknown);
    p.link = 1;
    p.bytes.resize(14);
    p.bytes[12] = 8;
    p.bytes[13] = 6;
    p.bytes.insert(p.bytes.end(),
                   {0, 1, 8, 0, 6, 4, 0, 1, 2, 0, 0, 0, 0, 1, 192, 0, 2, 1, 0, 0, 0, 0, 0, 0, 192, 0, 2, 9});
    decode(p);
    CHECK(p.proto == Protocol::ARP && p.to.text() == "192.0.2.9");
    auto tcp = d[0];
    tcp.bytes.resize(54);
    auto message = d[1].bytes.size() - 42;
    tcp.bytes.insert(tcp.bytes.end(), {0, uint8_t(message)});
    tcp.bytes.insert(tcp.bytes.end(), d[1].bytes.begin() + 42, d[1].bytes.end());
    auto len = tcp.bytes.size() - 14;
    tcp.bytes[16] = len >> 8;
    tcp.bytes[17] = len;
    tcp.bytes[37] = 53;
    tcp.bytes[36] = 0;
    decode(tcp);
    CHECK(tcp.proto == Protocol::DNS);
    tcp.bytes[55]++;
    decode(tcp);
    CHECK(tcp.proto == Protocol::TCP);
}
void filtersAndClock() {
    auto d = demo();
    Filter f;
    std::string err;
    auto local = std::set{d[0].from};
    CHECK(expression("protocol = dns", f, err) && f.accepts(d[1], local) && !f.accepts(d[0], local));
    CHECK(!expression("port = 70000", f, err) && f.proto == Protocol::DNS);
    CHECK(expression("port = 53", f, err) && f.accepts(d[1], local) && f.accepts(d[3], local));
    CHECK(expression("length >= 512", f, err) && f.accepts(d[0], local) && !f.accepts(d[1], local));
    CHECK(expression("length <= 64", f, err) && f.accepts(d[4], local));
    CHECK(direction(d[0], local) == Direction::Out && direction(d[2], local) == Direction::In);
    CHECK(elapsedFrom(980, 1000, 10) == 10 && elapsedFrom(1020, 1000, 10) == 20);
    CHECK(elapsedFrom(INT64_MAX, INT64_MIN, 0) == INT64_MAX - life);
    std::deque<Packet> records(d.begin(), d.end());
    CHECK(afterTime(records, 3500 * ms) == 10);
    CHECK(csvCell("  =SUM(A1)") == "\"'  =SUM(A1)\"" && csvCell("a,\"b") == "\"a,\"\"b\"");
    Store store{{2, 3000}};
    store.append(d[0]);
    store.append(d[1]);
    store.append(d[2]);
    CHECK(!store.get(1) && store.get(2)->id == 2 && store.evicted == 1);
}
void scheduling() {
    CHECK(phase(-1).opacity == 0 && phase(0).progress > 0 && phase(0).progress < .04);
    CHECK(phase(travel / 2).progress > .49 && phase(travel / 2).progress < .52);
    CHECK(phase(travel).progress == 1 && phase(travel + hold).opacity == 1);
    CHECK(std::abs(phase(travel + hold + fade / 2).opacity - .5) < .001 && phase(life).opacity == 0);
    CHECK(widthFor(64) < widthFor(512) && widthFor(512) < widthFor(1500) && widthFor(99999) == 6);
    auto d = demo();
    auto a = d[0].from, b = d[0].to;
    std::vector<Event> events;
    for (int i = 0; i < 300; ++i)
        events.push_back({uint64_t(i + 1), a, b, Protocol::TCP, 1500, 0});
    events.push_back({301, b, a, Protocol::UDP, 512, 0});
    for (int i = 1; i <= 20; ++i)
        events.push_back(
            {uint64_t(301 + i), a, Address::parse("198.51.100." + std::to_string(i)), Protocol::ICMP, 64, 0});
    Scheduler s;
    s.admit(events, 0);
    CHECK(s.active.size() <= s.budget && s.skipped + s.active.size() == events.size());
    CHECK(std::count_if(s.active.begin(), s.active.end(),
                        [&](auto &x) { return x.from == a && x.to == b; }) == 3);
    CHECK(std::any_of(s.active.begin(), s.active.end(), [&](auto &x) { return x.id == 321; }));
    s.prune(life);
    CHECK(s.active.empty());
    events = {
        {1, a, b, Protocol::TCP, 100, 0}, {2, b, a, Protocol::TCP, 444, 0}, {3, a, b, Protocol::UDP, 90, 0}};
    s.admit(events, 0);
    CHECK(s.active.size() == 3 && s.active[1].from == b && s.active[1].to == a);
    CHECK(s.active[0].lane != s.active[2].lane);
}
Feed collect(const std::filesystem::path &path, Limits l = {}) {
    Reader r(path.string(), false, l);
    Feed out;
    for (int i = 0; i < 2000; ++i) {
        auto feed = r.take();
        out.packets.insert(out.packets.end(), std::make_move_iterator(feed.packets.begin()),
                           std::make_move_iterator(feed.packets.end()));
        if (feed.state != State::Starting && feed.state != State::Reading) {
            out.state = feed.state;
            out.message = feed.message;
            return out;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Reader did not finish");
}
void files() {
    Temp tmp;
    auto d = demo();
    d.resize(4);
    d[0].timestamp += 73;
    d[1].timestamp = d[0].timestamp;
    d[2].timestamp = d[0].timestamp - 12345;
    d[3].bytes.resize(60);
    for (std::string type : {"pcap", "nano", "pcapng"}) {
        auto path = tmp.path / type;
        fixtures::write(path, type == "pcapng" ? fixtures::pcapng(d) : fixtures::classic(d, type == "nano"));
        auto feed = collect(path);
        CHECK(feed.state == State::Finished && feed.packets.size() == 4);
        auto &p = feed.packets;
        CHECK(p[0].timestamp == (type == "nano" ? d[0].timestamp : d[0].timestamp / 1000 * 1000));
        CHECK(p[0].proto == Protocol::TCP && p[0].original == 1248 && p[1].proto == Protocol::DNS);
        CHECK(p[0].elapsed == 0 && p[1].elapsed == 0 && p[2].elapsed == 0 && p[2].timestamp < p[0].timestamp);
        CHECK(p[3].original != p[3].bytes.size() && p[3].bytes == d[3].bytes);
        feed = collect(path, {2, 100000});
        CHECK(feed.state == State::Limited && feed.packets.size() == 2);
    }
    auto bad = tmp.path / "bad";
    fixtures::write(bad, {1, 2, 3, 4});
    CHECK(collect(bad).state == State::Error);
    auto unsupported = tmp.path / "other";
    fixtures::write(unsupported, fixtures::classic(d, false, 147));
    CHECK(collect(unsupported).state == State::Error);
    // Force file backpressure and cancel twice while the bounded mailbox is full.
    auto many = demo();
    auto original = many;
    for (int i = 0; i < 32; ++i)
        many.insert(many.end(), original.begin(), original.end());
    auto large = tmp.path / "many";
    fixtures::write(large, fixtures::classic(many));
    for (int i = 0; i < 2; ++i) {
        Reader r(large.string(), false);
        std::this_thread::sleep_for(30ms);
        auto t = std::chrono::steady_clock::now();
        r.stop();
        CHECK(std::chrono::steady_clock::now() - t < 500ms);
        auto f = r.take();
        CHECK(f.state == State::Stopped && f.packets.size() <= 1024);
    }
    CHECK(collect(large).packets.size() == many.size());
}
void workload() {
    auto d = demo();
    Store store;
    Scheduler scheduler;
    uint64_t n = 0;
    std::vector<Event> batch;
    auto start = std::chrono::steady_clock::now();
    auto end = start + 5s;
    do {
        for (int k = 0; k < 256; k++) {
            auto p = d[n % d.size()];
            p.id = ++n;
            p.elapsed = n * 20000;
            decode(p);
            batch.push_back({n, p.from, p.to, p.proto, p.length(), p.elapsed});
            store.append(std::move(p));
        }
        scheduler.admit(batch, batch.back().start);
        batch.clear();
    } while (std::chrono::steady_clock::now() < end);
    auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(store.data.size() <= store.limits.records && store.bytes <= store.limits.bytes &&
          scheduler.active.size() <= Scheduler::budget);
    CHECK(store.evicted + store.data.size() == n && scheduler.skipped > 0);
    std::cout << n << " packets in " << seconds << " s, " << uint64_t(n / seconds) << " packets/s, "
              << store.data.size() << " retained, " << store.bytes << " retained frame bytes, "
              << store.evicted << " evicted, " << scheduler.skipped << " animations skipped\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--workload") {
            workload();
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--fixture") {
            fixtures::write(argv[2], fixtures::classic(demo()));
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--check-live") {
            for (int n = 0; n < 2; ++n) {
                Reader reader(argv[2], true);
                std::this_thread::sleep_for(250ms);
                auto feed = reader.take();
                reader.stop();
                std::cout << "Start " << n + 1 << ": "
                          << (feed.state == State::Running ? "running" : feed.message) << "\n";
            }
            return 0;
        }
        decoder();
        filtersAndClock();
        scheduling();
        files();
        std::cout << "Decoder, filters, scheduler, retention, capture readers and cancellation passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
