#pragma once
#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pf {
using Bytes = std::vector<uint8_t>;
enum class Protocol { TCP, UDP, DNS, ICMP, ARP, Unknown };
struct Style {
    const char *name, *color;
};
Style style(Protocol p);

struct Address {
    enum Family { None, IPv4, IPv6, Mac } family = None;
    std::array<uint8_t, 16> value{};
    auto operator<=>(const Address &) const = default;
    explicit operator bool() const { return family != None; }
    std::string text() const;
    static Address read(Family family, std::span<const uint8_t> bytes);
    static Address parse(const std::string &text);
};

struct Field {
    std::string layer, name, value;
    size_t offset, length;
};
struct Packet {
    uint64_t id = 0;
    int64_t timestamp = 0, elapsed = 0; // nanoseconds
    uint32_t original = 0;
    int link = 1;
    Address from, to;
    std::optional<uint16_t> sport, dport;
    Protocol proto = Protocol::Unknown;
    std::string summary, status;
    Bytes bytes;
    uint32_t length() const { return original ? original : static_cast<uint32_t>(bytes.size()); }
};
bool supportedLink(int link);
void decode(Packet &p, std::vector<Field> *fields = nullptr);
std::vector<Packet> demo();
} // namespace pf
