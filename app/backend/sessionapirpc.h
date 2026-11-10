#pragma once
// Transitional control-only RPC. No Qt owner, event loop, network or media data.
// The existing pipe reader completes requests directly even while SDL's owner
// is waiting. IDs are process-lifetime unique; late replies cannot be reused.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace SessionApiRpc {
struct Timeout : std::runtime_error { Timeout():std::runtime_error("Host API timed out") {} };
class Broker {
public:
    using Sender = std::function<bool(uint64_t, const std::string&)>;
    void install(Sender sender) {
        std::lock_guard<std::mutex> lock(mutex);
        if (configured) throw std::logic_error("Session API already configured");
        configured=true; send=std::move(sender);
    }
    bool enabled() { std::lock_guard<std::mutex> lock(mutex); return configured; }
    std::string call(const std::string& request, unsigned timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex);
        if (closed || !send || nextId>9007199254740991ULL) throw std::runtime_error("Host API disconnected");
        const auto id=nextId++;
        pending.emplace(id, Result{});
        // Serialize publication with close; sender writes only the private pipe
        // and never re-enters the broker or touches a Qt object.
        if (!send(id,request)) { pending.erase(id); throw std::runtime_error("Host API pipe failed"); }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
        const bool ready=changed.wait_until(lock,deadline,[&]{return closed || pending.at(id).ready;});
        auto result=std::move(pending.at(id)); pending.erase(id);
        if (!ready) throw Timeout();
        if (!result.ready) throw std::runtime_error("Host API disconnected");
        return result.value;
    }
    bool complete(uint64_t id, std::string value) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it=pending.find(id);
        if (closed || it==pending.end() || it->second.ready) return false;
        it->second={true,std::move(value)}; changed.notify_all(); return true;
    }
    void close() { std::lock_guard<std::mutex> lock(mutex); closed=true; changed.notify_all(); }
private:
    struct Result { bool ready=false; std::string value; };
    std::mutex mutex;
    std::condition_variable changed;
    std::map<uint64_t,Result> pending;
    Sender send;
    uint64_t nextId=1;
    bool configured=false, closed=false;
};
inline Broker& broker() { static Broker instance; return instance; }
}
