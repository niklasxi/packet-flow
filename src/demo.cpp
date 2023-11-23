#include "packet.hpp"
#include <algorithm>

namespace pf {
namespace {
void put16(Bytes &b, size_t at, unsigned n) {
    b[at] = n >> 8;
    b[at + 1] = n;
}
uint16_t checksum(std::span<const uint8_t> data) {
    uint32_t sum = 0;
    for (size_t i = 0; i < data.size(); i += 2)
        sum += (unsigned(data[i]) << 8) + (i + 1 < data.size() ? data[i + 1] : 0);
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return uint16_t(~sum);
}
Packet frame(int kind, bool incoming, int length, uint64_t id, int ms) {
    const bool v6 = kind == 3;
    const auto src = Address::parse(v6 ? "2001:db8::24" : "192.0.2.24");
    const auto dst = Address::parse(kind == 0   ? "203.0.113.42"
                                    : kind == 1 ? "192.0.2.53"
                                    : kind == 2 ? "203.0.113.42"
                                    : kind == 3 ? "2001:db8::8"
                                                : "192.0.2.1");
    auto from = incoming ? dst : src, to = incoming ? src : dst;
    uint8_t ipProto = kind == 0 ? 6 : kind == 1 || kind == 2 ? 17 : v6 ? 58 : 1;
    Bytes dns = {0x26,
                 0x71,
                 uint8_t(incoming ? 0x81 : 1),
                 uint8_t(incoming ? 0x80 : 0),
                 0,
                 1,
                 0,
                 uint8_t(incoming),
                 0,
                 0,
                 0,
                 0,
                 7,
                 'e',
                 'x',
                 'a',
                 'm',
                 'p',
                 'l',
                 'e',
                 4,
                 't',
                 'e',
                 's',
                 't',
                 0,
                 0,
                 1,
                 0,
                 1};
    if (incoming)
        dns.insert(dns.end(), {0xc0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 203, 0, 113, 42});
    size_t net = 14, iplen = v6 ? 40 : 20, t = net + iplen;
    if (kind == 1)
        length = int(t + 8 + dns.size());
    Bytes b(std::max<size_t>(length, t + (ipProto == 6 ? 20 : 8)), 0);
    for (int i = 0; i < 12; ++i)
        b[i] = i % 6 == 0 ? 2 : 0;
    b[5] = incoming ? 24 : 42;
    b[11] = incoming ? 42 : 24;
    put16(b, 12, v6 ? 0x86dd : 0x800);
    if (v6) {
        b[net] = 0x60;
        put16(b, net + 4, b.size() - t);
        b[net + 6] = ipProto;
        b[net + 7] = 64;
    } else {
        b[net] = 0x45;
        put16(b, net + 2, b.size() - net);
        put16(b, net + 4, id);
        b[net + 8] = 64;
        b[net + 9] = ipProto;
    }
    size_t n = v6 ? 16 : 4;
    std::copy_n(from.value.begin(), n, b.begin() + net + (v6 ? 8 : 12));
    std::copy_n(to.value.begin(), n, b.begin() + net + (v6 ? 24 : 16));
    if (!v6)
        put16(b, net + 10, checksum(std::span(b).subspan(net, 20)));
    size_t sumAt = t + 2;
    if (ipProto == 6 || ipProto == 17) {
        int service = kind == 1 ? 53 : 443;
        put16(b, t, incoming ? service : 52416);
        put16(b, t + 2, incoming ? 52416 : service);
        if (ipProto == 6) {
            put16(b, t + 6, id * 13);
            b[t + 12] = 0x50;
            b[t + 13] = 0x18;
            put16(b, t + 14, 65535);
            sumAt = t + 16;
        } else {
            put16(b, t + 4, b.size() - t);
            sumAt = t + 6;
            if (kind == 1)
                std::copy(dns.begin(), dns.end(), b.begin() + t + 8);
        }
    } else {
        b[t] = v6 ? (incoming ? 129 : 128) : (incoming ? 0 : 8);
        put16(b, t + 4, 21);
        put16(b, t + 6, id);
    }
    Bytes input;
    if (ipProto != 1) {
        input.insert(input.end(), from.value.begin(), from.value.begin() + n);
        input.insert(input.end(), to.value.begin(), to.value.begin() + n);
        unsigned len = b.size() - t;
        if (v6)
            input.insert(input.end(), {0, 0, uint8_t(len >> 8), uint8_t(len), 0, 0, 0, ipProto});
        else
            input.insert(input.end(), {0, ipProto, uint8_t(len >> 8), uint8_t(len)});
    }
    input.insert(input.end(), b.begin() + t, b.end());
    auto sum = checksum(input);
    put16(b, sumAt, sum ? sum : 65535);
    Packet p;
    p.id = id;
    p.bytes = std::move(b);
    p.original = p.bytes.size();
    p.elapsed = int64_t(ms) * 1000000;
    p.timestamp = 1735732800000000000LL + p.elapsed;
    decode(p);
    return p;
}
} // namespace
std::vector<Packet> demo() {
    std::vector<Packet> v;
    auto add = [&](int at, int kind, bool incoming, int bytes) {
        v.push_back(frame(kind, incoming, bytes, v.size() + 1, at));
    };
    for (int cycle = 0; cycle < 4; ++cycle) {
        int t = cycle * 6500;
        add(t, 0, false, 1248);
        add(t + 150, 1, false, 64);
        add(t + 307, 2, true, 512);
        add(t + 1150, 1, true, 88);
        add(t + 1500, 4, false, 64);
        add(t + 1660, 4, true, 64);
        add(t + 2480, 3, false, 98);
        add(t + 2710, 3, true, 98);
        add(t + 3500, 0, false, 1500);
        add(t + 3500, 0, true, 660);
        add(t + 3600, 0, false, 920);
        if (cycle == 2)
            for (int j = 0; j < 180; ++j)
                add(t + 3880 + j * 3, j % 5, j % 3 == 0, 64 + (j * 97) % 1400);
    }
    return v;
}
} // namespace pf
