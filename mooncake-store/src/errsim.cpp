#include "errsim.h"

#ifndef NDEBUG

#include <glog/logging.h>

namespace mooncake {

// ─── Global registry ─────────────────────────────────────────────────────

std::unordered_map<std::string, ErrsimPoint*>& ErrsimPoint::registry() {
    static std::unordered_map<std::string, ErrsimPoint*> reg;
    return reg;
}

std::mutex& ErrsimPoint::registry_mutex() {
    static std::mutex mtx;
    return mtx;
}

ErrsimPoint::ErrsimPoint(const char* name) : name_(name) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    registry()[name] = this;
}

// ─── check() ──────────────────────────────────────────────────────────────

int ErrsimPoint::check(const std::string& key) {
    int err = err_.load(std::memory_order_acquire);
    if (err == 0) return 0;  // fast path: not active

    // Key filter: if match_key_ is non-empty the call must match it.
    {
        std::lock_guard<std::mutex> lock(match_key_mutex_);
        if (!match_key_.empty() && match_key_ != key) return 0;
    }

    // Count-down logic
    int rem = remain_.load(std::memory_order_acquire);
    if (rem == 0) return 0;  // deactivated by count exhaustion
    if (rem > 0) {
        // Finite: decrement, stop when reaching 0
        remain_.fetch_sub(1, std::memory_order_acq_rel);
        if (remain_.load(std::memory_order_acquire) < 0) {
            // Another thread raced and exhausted the counter
            remain_.store(0, std::memory_order_release);
            err_.store(0, std::memory_order_release);
            return 0;
        }
    }
    // rem < 0 means infinite

    LOG(INFO) << "[ERRSIM] Injecting error " << err << " at point " << name_
              << (key.empty() ? "" : " for key=" + key);
    return err;
}

// ─── Test-side API ────────────────────────────────────────────────────────

void ErrsimPoint::activate(const std::string& name, int err_code,
                           const std::string& match_key, int times) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto it = registry().find(name);
    if (it == registry().end()) {
        LOG(WARNING) << "[ERRSIM] activate: unknown point '" << name << "'";
        return;
    }
    ErrsimPoint* pt = it->second;
    {
        std::lock_guard<std::mutex> klock(pt->match_key_mutex_);
        pt->match_key_ = match_key;
    }
    pt->remain_.store(times, std::memory_order_release);
    pt->err_.store(err_code, std::memory_order_release);
}

void ErrsimPoint::reset(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto it = registry().find(name);
    if (it == registry().end()) return;
    ErrsimPoint* pt = it->second;
    pt->err_.store(0, std::memory_order_release);
    pt->remain_.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> klock(pt->match_key_mutex_);
        pt->match_key_.clear();
    }
}

ErrsimPoint* ErrsimPoint::get(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto it = registry().find(name);
    return it != registry().end() ? it->second : nullptr;
}

}  // namespace mooncake

#endif  // NDEBUG
