// Copyright 2025 Mooncake Authors
//
// Errsim: lightweight, non-intrusive deterministic error injection for tests.
//
// Inspired by OceanBase's errsim / tracepoint mechanism.
//
// ── Placement in production code ──────────────────────────────────────────
//
//   // At .cpp file scope (one per point):
//   ERRSIM_POINT_DEF(EP_STORAGE_ADAPTOR_OFFLOAD);
//
//   // Inside a function, at the point where failure should be simulated:
//   ERRSIM_INJECT(EP_STORAGE_ADAPTOR_OFFLOAD, kv.key, continue);
//
// ── Activation in tests ───────────────────────────────────────────────────
//
//   // Fail every call to the point:
//   ErrsimGuard g(EP_MY_POINT, ErrorCode::INTERNAL_ERROR);
//
//   // Fail only when the key equals "key2":
//   ErrsimGuard g(EP_MY_POINT, ErrorCode::INTERNAL_ERROR, "key2");
//
//   // Manual (no RAII):
//   ErrsimPoint::activate("EP_MY_POINT", toInt(ErrorCode::INTERNAL_ERROR));
//   ErrsimPoint::reset("EP_MY_POINT");
//
// ── Release builds ────────────────────────────────────────────────────────
//   All macros compile to ((void)0) — zero overhead.
//
#pragma once

#ifndef NDEBUG

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mooncake {

// ─── Core injection point ─────────────────────────────────────────────────

class ErrsimPoint {
   public:
    // Register this point in the global table under `name`.
    explicit ErrsimPoint(const char* name);

    // Called at each injection site. Returns non-zero when the point is active
    // AND (match_key is empty OR match_key == key).
    // Passing an empty `key` matches any active point unconditionally.
    int check(const std::string& key = "");

    // ── Test-side API ────────────────────────────────────────────────────

    // Activate point `name`: every call whose key matches `match_key` (empty =
    // all keys) will return `err_code`.  `times` = -1 means infinite; positive
    // integer means "fire N times then reset".
    static void activate(const std::string& name, int err_code,
                         const std::string& match_key = "", int times = -1);

    // Deactivate a named point.
    static void reset(const std::string& name);

    // Look up point by name (used by ErrsimGuard).
    static ErrsimPoint* get(const std::string& name);

    const char* name() const { return name_; }

   private:
    const char* name_;
    std::atomic<int> err_{0};     // 0 == inactive
    std::atomic<int> remain_{0};  // -1 = infinite; 0 = inactive; >0 = count
    std::string match_key_;       // empty = match all
    std::mutex match_key_mutex_;

    static std::unordered_map<std::string, ErrsimPoint*>& registry();
    static std::mutex& registry_mutex();
};

// ─── RAII helper for tests ────────────────────────────────────────────────

class ErrsimGuard {
   public:
    // `match_key`: empty string means "fail ALL keys"; non-empty means "only
    // fail when the injection site key equals match_key".
    ErrsimGuard(ErrsimPoint& point, int err_code,
                const std::string& match_key = "", int times = -1)
        : name_(point.name()) {
        ErrsimPoint::activate(name_, err_code, match_key, times);
    }
    ~ErrsimGuard() { ErrsimPoint::reset(name_); }

   private:
    std::string name_;
};

}  // namespace mooncake

// ─── Macros used in production code ──────────────────────────────────────

// Define a static named injection point (once per .cpp file, at namespace
// or class-implementation scope, NOT inside a function).
#define ERRSIM_POINT_DEF(name) static ::mooncake::ErrsimPoint name(#name)

// Inject at this site.  `key` is the discriminator string (e.g. a storage
// key); `on_err` is a statement executed when the point fires (e.g. `continue`
// or `return tl::make_unexpected(ErrorCode::INTERNAL_ERROR)`).
//
// Example:
//   ERRSIM_INJECT(EP_ADAPTOR_OFFLOAD, kv.key, continue);
#define ERRSIM_INJECT(point, key, on_err) \
    do {                                  \
        if ((point).check(key) != 0) {    \
            on_err;                       \
        }                                 \
    } while (0)

// Convenience: inject returning a tl::expected unexpected value.
//
// Example:
//   ERRSIM_INJECT_EXPECTED(EP_WRITE, "", ErrorCode::FILE_WRITE_FAIL);
#define ERRSIM_INJECT_EXPECTED(point, key, err_code) \
    ERRSIM_INJECT(point, key, return tl::make_unexpected(err_code))

#else  // NDEBUG ─── Release: zero overhead ─────────────────────────────────

#define ERRSIM_POINT_DEF(name) ((void)0)
#define ERRSIM_INJECT(point, key, on_err) ((void)0)
#define ERRSIM_INJECT_EXPECTED(point, key, err) ((void)0)

namespace mooncake {

// Stub types so test-only references compile cleanly even in release.
struct ErrsimPoint {
    explicit ErrsimPoint(const char*) {}
    static void activate(const std::string&, int, const std::string& = "",
                         int = -1) {}
    static void reset(const std::string&) {}
    static ErrsimPoint* get(const std::string&) { return nullptr; }
    const char* name() const { return ""; }
};

struct ErrsimGuard {
    ErrsimGuard(ErrsimPoint&, int, const std::string& = "", int = -1) {}
};

}  // namespace mooncake

#endif  // NDEBUG
