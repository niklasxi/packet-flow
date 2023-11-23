#pragma once
#include "engine.hpp"
#include <memory>
#include <mutex>
#include <thread>

namespace pf {
struct Interface {
    std::string name, description;
    std::set<Address> addresses;
};
std::vector<Interface> interfaces(std::string &error);
enum class State { Starting, Reading, Running, Stopped, Finished, Limited, Error };
struct Feed {
    std::vector<Packet> packets;
    State state = State::Starting;
    uint64_t delivered = 0, discarded = 0;
    std::optional<uint64_t> backendDrops;
    double progress = 0;
    std::string message;
};
class Source {
  public:
    virtual ~Source() = default;
    virtual Feed take() = 0;
    virtual void requestStop() = 0;
};
class Reader : public Source {
  public:
    Reader(std::string source, bool live, Limits limits = {});
    ~Reader() override;
    Feed take() override;
    void requestStop() override;
    void stop();

  private:
    void read(std::stop_token, const std::string &, bool, Limits);
    std::mutex mutex;
    Feed buffer;
    size_t queuedBytes = 0;
    std::jthread worker;
};
class CaptureBackend {
  public:
    virtual ~CaptureBackend() = default;
    virtual std::vector<Interface> devices(std::string &error) { return interfaces(error); }
    virtual std::unique_ptr<Source> open(const std::string &path, bool live) {
        return std::make_unique<Reader>(path, live);
    }
};
} // namespace pf
