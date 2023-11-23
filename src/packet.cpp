#include "packet.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <limits>
#include <netinet/in.h>
#include <pcap/dlt.h>

namespace pf {
Style style(Protocol p) {
    switch (p) {
    case Protocol::TCP:
        return {"TCP", "#e99a1c"};
    case Protocol::UDP:
        return {"UDP", "#0eb3da"};
    case Protocol::DNS:
        return {"DNS", "#9969ec"};
    case Protocol::ICMP:
        return {"ICMP", "#31ba94"};
    case Protocol::ARP:
        return {"ARP", "#4c82d9"};
    default:
        return {"Unknown", "#7f8b9a"};
    }
}
Address Address::read(Family f, std::span<const uint8_t> b) {
    Address a;
    size_t n = f == IPv4 ? 4 : f == IPv6 ? 16 : f == Mac ? 6 : 0;
    if (n && b.size() >= n) {
        a.family = f;
        std::copy_n(b.begin(), n, a.value.begin());
    }
    return a;
}
Address Address::parse(const std::string &s) {
    Address a;
    if (inet_pton(AF_INET, s.c_str(), a.value.data()) == 1)
        a.family = IPv4;
    else if (inet_pton(AF_INET6, s.c_str(), a.value.data()) == 1)
        a.family = IPv6;
    else {
        unsigned m[6];
        int end = 0;
        if (s.size() == 17 &&
            sscanf(s.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%n", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &end) ==
                6 &&
            end == 17) {
            a.family = Mac;
            for (int i = 0; i < 6; ++i)
                a.value[i] = uint8_t(m[i]);
        }
    }
    return a;
}
std::string Address::text() const {
    char buf[INET6_ADDRSTRLEN]{};
    if (family == Mac)
        snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", value[0], value[1], value[2], value[3],
                 value[4], value[5]);
    else if (family != None)
        inet_ntop(family == IPv4 ? AF_INET : AF_INET6, value.data(), buf, sizeof(buf));
    return buf;
}
bool supportedLink(int l) { return l == DLT_EN10MB || l == DLT_NULL || l == DLT_LOOP || l == DLT_RAW; }

namespace {
using View = std::span<const uint8_t>;
uint16_t word(View b, size_t i) { return uint16_t(b[i]) << 8 | b[i + 1]; }
bool has(size_t end, size_t pos, size_t n) { return pos <= end && n <= end - pos; }

bool dnsName(View b, size_t &cursor, std::string *output) {
    size_t i = cursor, expanded = 0;
    bool jumped = false;
    for (int steps = 0; steps < 128 && i < b.size(); ++steps) {
        auto n = b[i++];
        if (!n) {
            if (!jumped)
                cursor = i;
            return expanded < 255;
        }
        if ((n & 0xc0) == 0xc0) {
            if (i >= b.size())
                return false;
            size_t target = ((n & 63) << 8) | b[i++];
            if (target < 12 || target >= i - 2)
                return false;
            if (!jumped)
                cursor = i;
            jumped = true;
            i = target;
            continue;
        }
        if (n > 63 || !has(b.size(), i, n) || (expanded += 1 + n) > 254)
            return false;
        if (output) {
            if (!output->empty())
                *output += ".";
            for (size_t k = i; k < i + n; ++k)
                *output += b[k] >= 33 && b[k] <= 126 ? char(b[k]) : '?';
        }
        i += n;
    }
    return false;
}
bool parseDns(View b, std::string &name, size_t &nameLength) {
    if (b.size() < 12 || (b[2] & 0x78) != 0 || (b[3] & 0x40) || (b[2] & 2))
        return false;
    unsigned q = word(b, 4), rr = unsigned(word(b, 6)) + word(b, 8) + word(b, 10);
    if (!q || q > 32 || rr > 256)
        return false;
    size_t i = 12;
    for (unsigned n = 0; n < q; ++n) {
        if (!dnsName(b, i, n == 0 ? &name : nullptr) || !has(b.size(), i, 4))
            return false;
        if (!n)
            nameLength = i - 12;
        if (!word(b, i) || !word(b, i + 2))
            return false;
        i += 4;
    }
    for (unsigned n = 0; n < rr; ++n) {
        if (!dnsName(b, i, nullptr) || !has(b.size(), i, 10))
            return false;
        unsigned type = word(b, i);
        auto len = word(b, i + 8);
        i += 10;
        if (!has(b.size(), i, len))
            return false;
        size_t end = i + len;
        if ((type == 1 && len != 4) || (type == 28 && len != 16))
            return false;
        // Validate embedded domain names in commonly encountered answers.
        if (type == 2 || type == 5 || type == 12 || type == 15 || type == 33 || type == 6) {
            size_t prefix = type == 15 ? 2 : type == 33 ? 6 : 0;
            if (prefix >= len)
                return false;
            size_t r = i + prefix;
            if (!dnsName(b.first(end), r, nullptr))
                return false;
            if (type == 6) {
                if (!dnsName(b.first(end), r, nullptr) || end - r != 20)
                    return false;
            } else if (r != end)
                return false;
        }
        i = end;
    }
    return i == b.size();
}
} // namespace

void decode(Packet &p, std::vector<Field> *fields) {
    View b = p.bytes;
    p.from = {};
    p.to = {};
    p.sport.reset();
    p.dport.reset();
    p.proto = Protocol::Unknown;
    p.summary = "Unrecognized protocol";
    p.status = p.original > b.size() ? "Capture truncated" : "Decoded";
    auto fail = [&](const char *message) {
        p.status = message;
        p.summary = message;
    };
    auto field = [&](const char *layer, const char *name, std::string value, size_t i, size_t n) {
        if (fields)
            fields->push_back({layer, name, std::move(value), i, n});
    };
    auto addr = [&](const char *layer, Address::Family f, size_t s, size_t d, size_t len) {
        p.from = Address::read(f, b.subspan(s, len));
        p.to = Address::read(f, b.subspan(d, len));
        if (fields) {
            field(layer, "Source", p.from.text(), s, len);
            field(layer, "Destination", p.to.text(), d, len);
        }
    };
    size_t i = 0, end = b.size();
    unsigned type = 0;
    if (p.link == DLT_EN10MB) {
        if (end < 14)
            return fail("Truncated Ethernet header");
        addr("Ethernet", Address::Mac, 6, 0, 6);
        type = word(b, 12);
        field("Ethernet", "EtherType", std::to_string(type), 12, 2);
        i = 14;
        int depth = 0;
        while (type == 0x8100 || type == 0x88a8 || type == 0x9100) {
            if (++depth > 4)
                return fail("VLAN nesting limit");
            if (!has(end, i, 4))
                return fail("Truncated VLAN tag");
            field("VLAN", "VLAN ID", std::to_string(word(b, i) & 4095), i, 2);
            type = word(b, i + 2);
            i += 4;
        }
    } else if (p.link == DLT_NULL || p.link == DLT_LOOP) {
        if (end < 4)
            return fail("Truncated loopback header");
        uint32_t family;
        memcpy(&family, b.data(), 4);
        if (p.link == DLT_LOOP || family > 255)
            family = ntohl(family);
        type = family == 2                                                    ? 0x800
               : family == 10 || family == 24 || family == 28 || family == 30 ? 0x86dd
                                                                              : 0;
        field("Loopback", "Address family", std::to_string(family), 0, 4);
        i = 4;
    } else if (p.link == DLT_RAW && end)
        type = (b[0] >> 4) == 4 ? 0x800 : (b[0] >> 4) == 6 ? 0x86dd : 0;
    else
        return fail("Unsupported or empty link layer");
    if (type == 0x806) {
        if (!has(end, i, 8))
            return fail("Truncated ARP header");
        size_t h = b[i + 4], a = b[i + 5];
        if (!has(end, i + 8, 2 * (h + a)))
            return fail("Truncated ARP addresses");
        p.proto = Protocol::ARP;
        p.summary = "ARP operation " + std::to_string(word(b, i + 6));
        field("ARP", "Operation", std::to_string(word(b, i + 6)), i + 6, 2);
        if (word(b, i + 2) == 0x800 && a == 4)
            addr("ARP", Address::IPv4, i + 8 + h, i + 8 + 2 * h + a, 4);
        return;
    }
    unsigned next = 0;
    bool fragmented = false;
    if (type == 0x800) {
        if (!has(end, i, 20))
            return fail("Truncated IPv4 header");
        size_t h = (b[i] & 15) * 4, len = word(b, i + 2);
        if (b[i] >> 4 != 4 || h < 20 || len < h)
            return fail("Invalid IPv4 length or version");
        addr("IPv4", Address::IPv4, i + 12, i + 16, 4);
        field("IPv4", "Total length", std::to_string(len), i + 2, 2);
        if (!has(end, i, h))
            return fail("Truncated IPv4 options");
        if (len > end - i)
            p.status = "Truncated IPv4 packet";
        end = std::min(end, i + len);
        next = b[i + 9];
        if (word(b, i + 6) & 0x1fff)
            return fail("Non-initial IPv4 fragment");
        fragmented = word(b, i + 6) & 0x2000;
        i += h;
    } else if (type == 0x86dd) {
        if (!has(end, i, 40) || b[i] >> 4 != 6)
            return fail("Invalid or truncated IPv6 header");
        addr("IPv6", Address::IPv6, i + 8, i + 24, 16);
        size_t len = word(b, i + 4);
        field("IPv6", "Payload length", std::to_string(len), i + 4, 2);
        if (len > end - i - 40)
            p.status = "Truncated IPv6 packet";
        next = b[i + 6];
        i += 40;
        end = std::min(end, i + len);
        int depth = 0;
        while (next == 0 || next == 43 || next == 44 || next == 60 || next == 51) {
            if (++depth > 8)
                return fail("IPv6 extension limit");
            if (!has(end, i, 2))
                return fail("Truncated IPv6 extension");
            size_t h = next == 44 ? 8 : next == 51 ? (b[i + 1] + 2) * 4 : (b[i + 1] + 1) * 8;
            if (!has(end, i, h))
                return fail("Invalid IPv6 extension length");
            if (next == 44) {
                if (word(b, i + 2) & 0xfff8)
                    return fail("Non-initial IPv6 fragment");
                fragmented = word(b, i + 2) & 1;
            }
            field("IPv6 extension", "Next header", std::to_string(b[i]), i, 1);
            next = b[i];
            i += h;
        }
    } else
        return fail("Unrecognized network protocol");
    if (fragmented)
        p.status = "First fragment";
    if (next == 1 || next == 58) {
        if (!has(end, i, 4))
            return fail("Truncated ICMP header");
        p.proto = Protocol::ICMP;
        const char *layer = next == 58 ? "ICMPv6" : "ICMP";
        p.summary =
            std::string(layer) + " type " + std::to_string(b[i]) + ", code " + std::to_string(b[i + 1]);
        field(layer, "Type", std::to_string(b[i]), i, 1);
        field(layer, "Code", std::to_string(b[i + 1]), i + 1, 1);
        return;
    }
    if (next != 6 && next != 17)
        return fail("Unrecognized IP payload");
    bool tcp = next == 6;
    size_t h = tcp ? 20 : 8;
    if (!has(end, i, h))
        return fail(tcp ? "Truncated TCP header" : "Truncated UDP header");
    if (tcp) {
        h = (b[i + 12] >> 4) * 4;
        if (h < 20 || !has(end, i, h))
            return fail("Invalid TCP data offset");
    }
    const char *layer = tcp ? "TCP" : "UDP";
    p.proto = tcp ? Protocol::TCP : Protocol::UDP;
    p.sport = word(b, i);
    p.dport = word(b, i + 2);
    p.summary = std::to_string(*p.sport) + " → " + std::to_string(*p.dport);
    field(layer, "Source port", std::to_string(*p.sport), i, 2);
    field(layer, "Destination port", std::to_string(*p.dport), i + 2, 2);
    if (tcp) {
        std::string flags;
        const char *names[] = {"FIN", "SYN", "RST", "PSH", "ACK", "URG", "ECE", "CWR"};
        for (int bit = 0; bit < 8; ++bit)
            if (b[i + 13] & (1 << bit)) {
                if (!flags.empty())
                    flags += " ";
                flags += names[bit];
            }
        p.summary += "  " + flags;
        field(layer, "Flags", flags, i + 13, 1);
    } else {
        size_t len = word(b, i + 4);
        field(layer, "Datagram length", std::to_string(len), i + 4, 2);
        if (len < 8 || len > end - i) {
            p.status = fragmented ? "First UDP fragment" : "Invalid or incomplete UDP length";
            return;
        }
        end = i + len;
    }
    i += h;
    if (fragmented || (*p.sport != 53 && *p.dport != 53))
        return;
    if (tcp) {
        if (!has(end, i, 2) || word(b, i) != end - i - 2)
            return;
        i += 2;
    }
    std::string name;
    size_t nameLen = 0;
    if (!parseDns(b.subspan(i, end - i), name, nameLen))
        return;
    p.proto = Protocol::DNS;
    p.summary = (b[i + 2] & 128 ? "Response · " : "Query · ") + name;
    field("DNS", "Transaction ID", std::to_string(word(b, i)), i, 2);
    field("DNS", "Question", name, i + 12, nameLen);
}
} // namespace pf
