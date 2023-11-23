#include "engine.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

namespace pf {
Phase phase(int64_t age) {
    if (age < 0 || age >= life)
        return {0, 0};
    return {0.018 + 0.982 * std::min(1., double(age) / travel),
            std::clamp(double(life - age) / fade, 0., 1.)};
}
double widthFor(uint32_t n) { return 1.5 + 4.5 * std::sqrt(std::clamp((double(n) - 64) / 1436., 0., 1.)); }
int64_t elapsedFrom(int64_t t, int64_t origin, int64_t previous) {
    if (t <= origin)
        return previous;
    uint64_t diff = uint64_t(t) - uint64_t(origin);
    return std::max(previous, int64_t(std::min<uint64_t>(diff, INT64_MAX - life)));
}
size_t afterTime(const std::deque<Packet> &data, int64_t t) {
    return std::upper_bound(data.begin(), data.end(), t,
                            [](auto v, const Packet &p) { return v < p.elapsed; }) -
           data.begin();
}
void Store::append(Packet p) {
    bytes += p.bytes.size();
    data.push_back(std::move(p));
    while (data.size() > limits.records || bytes > limits.bytes) {
        bytes -= data.front().bytes.size();
        data.pop_front();
        ++evicted;
    }
}
const Packet *Store::get(uint64_t id) const {
    if (data.empty() || id < data.front().id || id > data.back().id)
        return nullptr;
    auto i = id - data.front().id;
    return i < data.size() && data[i].id == id ? &data[i] : nullptr;
}
Direction direction(const Packet &p, const std::set<Address> &local) {
    bool a = local.contains(p.from), b = local.contains(p.to);
    return a == b ? Direction::Other : a ? Direction::Out : Direction::In;
}
bool Filter::accepts(const Packet &p, const std::set<Address> &local) const {
    return (!proto || p.proto == *proto) && (!port || port == p.sport || port == p.dport) &&
           (!peer || *peer == p.from || *peer == p.to) && p.length() >= min && p.length() <= max &&
           (dir == Direction::Any || dir == direction(p, local));
}
bool expression(const std::string &text, Filter &f, std::string &error) {
    std::istringstream in(text);
    std::string key, op, value, extra;
    Filter next;
    if (!(in >> key)) {
        f = next;
        error.clear();
        return true;
    }
    if (in >> op >> value && !(in >> extra)) {
        if (key == "protocol" && op == "=") {
            for (auto p : {Protocol::TCP, Protocol::UDP, Protocol::DNS, Protocol::ICMP, Protocol::ARP,
                           Protocol::Unknown}) {
                std::string n = style(p).name;
                std::transform(n.begin(), n.end(), n.begin(),
                               [](unsigned char c) { return char(std::tolower(c)); });
                if (n == value) {
                    next.proto = p;
                    f = next;
                    error.clear();
                    return true;
                }
            }
        } else {
            uint32_t n = 0;
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (ec == std::errc() && ptr == value.data() + value.size()) {
                if (key == "port" && op == "=" && n <= 65535)
                    next.port = uint16_t(n);
                else if (key == "length" && op == ">=")
                    next.min = n;
                else if (key == "length" && op == "<=")
                    next.max = n;
                else {
                    error = "Use port = 0…65535 or length >= / <= bytes.";
                    return false;
                }
                f = next;
                error.clear();
                return true;
            }
        }
    }
    error = "Examples: protocol = dns, port = 53, length >= 512. Use spaces around the operator.";
    return false;
}
void Scheduler::prune(int64_t now) {
    std::erase_if(active, [&](auto &a) { return now - a.start >= life; });
}
void Scheduler::admit(std::span<const Event> events, int64_t now) {
    prune(now);
    std::map<Route, std::deque<const Event *>> pending;
    for (auto &e : events) {
        if (!e.from || !e.to || now - e.start >= life) {
            ++skipped;
            continue;
        }
        Route route = std::minmax(e.from, e.to);
        auto &q = pending[route];
        if (q.size() == routeBudget) {
            ++skipped;
            continue;
        }
        q.push_back(&e);
    }
    while (!pending.empty() && active.size() < budget) {
        auto it = pending.upper_bound(cursor);
        if (it == pending.end())
            it = pending.begin();
        cursor = it->first;
        auto &q = it->second;
        auto &e = *q.front();
        std::array<bool, routeBudget> used{};
        for (auto &a : active)
            if (Route(std::minmax(a.from, a.to)) == cursor)
                used[a.lane] = true;
        auto slot = std::find(used.begin(), used.end(), false);
        if (slot == used.end()) {
            skipped += q.size();
            pending.erase(it);
            continue;
        }
        active.push_back({e, int(slot - used.begin())});
        q.pop_front();
        if (q.empty())
            pending.erase(it);
    }
    for (auto &[_, q] : pending)
        skipped += q.size();
}
std::string csvCell(std::string s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if ((!s.empty() && (s[0] == '\t' || s[0] == '\r' || s[0] == '\n')) ||
        (first != s.npos && std::string("=+-@").find(s[first]) != s.npos))
        s.insert(0, "'");
    std::string result = "\"";
    for (char c : s) {
        if (c == '"')
            result += '"';
        result += c;
    }
    return result + '"';
}
} // namespace pf
