#include "capture.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <netinet/in.h>
#include <pcap/pcap.h>

namespace pf {
using namespace std::chrono_literals;
std::vector<Interface> interfaces(std::string &error) {
    char err[PCAP_ERRBUF_SIZE]{};
    pcap_if_t *raw = nullptr;
    if (pcap_findalldevs(&raw, err) < 0) {
        error = err;
        return {};
    }
    std::unique_ptr<pcap_if_t, decltype(&pcap_freealldevs)> owner(raw, pcap_freealldevs);
    std::vector<Interface> result;
    for (auto *d = raw; d; d = d->next) {
        Interface iface{d->name, d->description ? d->description : "", {}};
        for (auto *a = d->addresses; a; a = a->next) {
            if (!a->addr)
                continue;
            if (a->addr->sa_family == AF_INET) {
                auto *v = reinterpret_cast<const sockaddr_in *>(a->addr);
                iface.addresses.insert(
                    Address::read(Address::IPv4, {reinterpret_cast<const uint8_t *>(&v->sin_addr), 4}));
            } else if (a->addr->sa_family == AF_INET6) {
                auto *v = reinterpret_cast<const sockaddr_in6 *>(a->addr);
                iface.addresses.insert(Address::read(Address::IPv6, {v->sin6_addr.s6_addr, 16}));
            }
        }
        result.push_back(std::move(iface));
    }
    return result;
}
Reader::Reader(std::string path, bool live, Limits limits)
    : worker(
          [this, path = std::move(path), live, limits](std::stop_token s) { read(s, path, live, limits); }) {}
Reader::~Reader() { stop(); }
void Reader::requestStop() { worker.request_stop(); }
void Reader::stop() {
    requestStop();
    if (worker.joinable())
        worker.join();
}
Feed Reader::take() {
    std::lock_guard lock(mutex);
    Feed out;
    out.packets.swap(buffer.packets);
    queuedBytes = 0;
    out.state = buffer.state;
    out.delivered = buffer.delivered;
    out.discarded = buffer.discarded;
    out.backendDrops = buffer.backendDrops;
    out.progress = buffer.progress;
    out.message = buffer.message;
    return out;
}
void Reader::read(std::stop_token token, const std::string &path, bool live, Limits limits) {
    char err[PCAP_ERRBUF_SIZE]{};
    auto state = [&](State s, std::string message = {}) {
        std::lock_guard l(mutex);
        buffer.state = s;
        buffer.message = std::move(message);
    };
    std::error_code ec;
    if (!live && !std::filesystem::is_regular_file(path, ec))
        return state(State::Error, "Choose a regular local capture file.");
    auto fileBytes = live ? 0 : std::filesystem::file_size(path, ec);
    std::unique_ptr<pcap_t, decltype(&pcap_close)> handle(
        live ? pcap_create(path.c_str(), err)
             : pcap_open_offline_with_tstamp_precision(path.c_str(), PCAP_TSTAMP_PRECISION_NANO, err),
        pcap_close);
    if (!handle)
        return state(State::Error, err);
    auto *h = handle.get();
    if (live) {
        if (pcap_set_snaplen(h, 65535) || pcap_set_promisc(h, 0) || pcap_set_timeout(h, 50))
            return state(State::Error, "Could not configure passive capture.");
        auto result = pcap_activate(h);
        if (result < 0) {
            std::string reason = pcap_geterr(h);
            if (result == PCAP_ERROR_PERM_DENIED || reason.find("ermission") != reason.npos ||
                reason.find("not permitted") != reason.npos)
                reason += ". Ask your administrator for BPF capture access (macOS ChmodBPF). Demo and file "
                          "replay are available.";
            return state(State::Error, reason);
        }
        if (pcap_setnonblock(h, 1, err) < 0)
            return state(State::Error, err);
    }
    int link = pcap_datalink(h);
    if (!supportedLink(link))
        return state(State::Error, "Unsupported capture link type " + std::to_string(link) +
                                       ". Use Ethernet, loopback, or raw IP.");
    state(live ? State::Running : State::Reading);
    uint64_t id = 0;
    size_t bytes = 0;
    int64_t first = 0, last = 0;
    auto sampleDrops = [&] {
        pcap_stat stats{};
        bool available = pcap_stats(h, &stats) == 0;
        std::lock_guard lock(mutex);
        buffer.backendDrops = available ? std::optional<uint64_t>(stats.ps_drop) : std::nullopt;
    };
    auto nextStats = std::chrono::steady_clock::now();
    while (!token.stop_requested()) {
        if (live && std::chrono::steady_clock::now() >= nextStats) {
            sampleDrops();
            nextStats = std::chrono::steady_clock::now() + 500ms;
        }
        pcap_pkthdr *header = nullptr;
        const u_char *data = nullptr;
        int result = pcap_next_ex(h, &header, &data);
        if (result == -2)
            return state(State::Finished);
        if (result < 0)
            return state(State::Error, pcap_geterr(h));
        if (!result) {
            std::this_thread::sleep_for(4ms);
            continue;
        }
        {
            std::lock_guard lock(mutex);
            ++buffer.delivered;
        }
        size_t n = header->caplen;
        if (!live && (id >= limits.records || n > limits.bytes - bytes))
            return state(State::Limited, "Loaded prefix only. The file exceeds the record or byte limit.");
        if (n > 1024 * 1024)
            return state(State::Error, "Record exceeds the 1 MiB per-frame limit.");
        bool room = false;
        do {
            {
                std::lock_guard l(mutex);
                room = buffer.packets.size() < 1024 && queuedBytes + n <= 4 * 1024 * 1024;
                if (live && !room)
                    ++buffer.discarded;
            }
            if (!room && !live)
                std::this_thread::sleep_for(3ms);
        } while (!room && !live && !token.stop_requested());
        if (!room)
            continue;
        Packet p;
        p.id = ++id;
        p.link = link;
        p.original = header->len;
        auto sec = header->ts.tv_sec;
        auto frac = int64_t(header->ts.tv_usec) * (live ? 1000 : 1);
        if (frac < 0 || frac >= 1000000000 || sec > (INT64_MAX - frac) / 1000000000 ||
            sec < INT64_MIN / 1000000000)
            return state(State::Error, "Timestamp outside the supported nanosecond range.");
        p.timestamp = int64_t(sec) * 1000000000 + frac;
        if (id == 1)
            first = p.timestamp;
        last = p.elapsed = elapsedFrom(p.timestamp, first, last);
        if (n)
            p.bytes.assign(data, data + n);
        decode(p);
        if (!live)
            bytes += n;
        std::lock_guard l(mutex);
        buffer.packets.push_back(std::move(p));
        queuedBytes += n;
        if (!live && fileBytes && id % 128 == 0) {
            if (auto *file = pcap_file(h))
                buffer.progress = std::clamp(double(ftello(file)) / fileBytes, 0., 1.);
        }
    }
    if (live)
        sampleDrops();
    state(State::Stopped);
}
} // namespace pf
