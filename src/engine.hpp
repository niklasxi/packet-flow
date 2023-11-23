#pragma once
#include "packet.hpp"
#include <deque>
#include <map>
#include <set>

namespace pf {
constexpr int64_t ms = 1000000, travel = 570 * ms, hold = 65 * ms, fade = 220 * ms,
                  life = travel + hold + fade;
struct Phase {
    double progress, opacity;
};
Phase phase(int64_t age);
double widthFor(uint32_t bytes);
int64_t elapsedFrom(int64_t timestamp, int64_t origin, int64_t previous);
size_t afterTime(const std::deque<Packet> &data, int64_t time);
struct Limits {
    size_t records = 24000, bytes = 48 * 1024 * 1024;
};
struct Store {
    Limits limits;
    std::deque<Packet> data;
    size_t bytes = 0, evicted = 0;
    void append(Packet p);
    const Packet *get(uint64_t id) const;
};
enum class Direction { Any, Out, In, Other };
Direction direction(const Packet &, const std::set<Address> &local);
struct Filter {
    std::optional<Protocol> proto;
    std::optional<uint16_t> port;
    std::optional<Address> peer;
    uint32_t min = 0, max = UINT32_MAX;
    Direction dir = Direction::Any;
    bool accepts(const Packet &, const std::set<Address> &local) const;
};
// One optional expression overlays the structured controls.
bool expression(const std::string &, Filter &, std::string &error);
struct Event {
    uint64_t id;
    Address from, to;
    Protocol proto;
    uint32_t bytes;
    int64_t start;
};
struct Arrow : Event {
    int lane;
};
class Scheduler {
  public:
    static constexpr size_t budget = 24, routeBudget = 3;
    std::vector<Arrow> active;
    uint64_t skipped = 0;
    void prune(int64_t now);
    void admit(std::span<const Event> events, int64_t now);

  private:
    using Route = std::pair<Address, Address>;
    Route cursor{};
};
std::string csvCell(std::string s);
} // namespace pf
