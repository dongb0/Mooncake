// Copyright 2025 KVCache.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

// Unified logging header for Mooncake.
// Provides glog-compatible LOG/VLOG/PLOG macros backed by spdlog.
// All Mooncake code should include this file instead of <glog/logging.h>.

#include <spdlog/spdlog.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace mooncake {
namespace logging {

// ---------------------------------------------------------------------------
// Logging configuration
// ---------------------------------------------------------------------------
struct LoggingConfig {
    std::string log_dir = "/tmp";                  // Default: /tmp (glog compat)
    size_t max_file_size = 100 * 1024 * 1024;      // 100 MB per file
    size_t max_files = 5;                          // Keep 5 rotated files
    size_t async_queue_size = 8192;                // Async queue capacity
    size_t async_thread_count = 1;                 // Async writer threads
    bool log_to_stderr = true;                     // Also log to stderr
    spdlog::level::level_enum min_level = spdlog::level::info;
};

/// Initialize the Mooncake logging system.
/// Call once at program startup (replaces google::InitGoogleLogging).
void InitMooncakeLogging(const char* program_name,
                         const LoggingConfig& config = LoggingConfig{});

/// Shutdown the logging system (flush + cleanup).
void ShutdownMooncakeLogging();

/// Set the verbose (VLOG) level.  VLOG(n) is active when n <= verbose_level.
void SetVerboseLevel(int level);

/// Get the current verbose level.
int GetVerboseLevel();

// ---------------------------------------------------------------------------
// Severity constants (matching glog names for macro usage)
// ---------------------------------------------------------------------------
constexpr int GLOG_INFO = 0;
constexpr int GLOG_WARNING = 1;
constexpr int GLOG_ERROR = 2;
constexpr int GLOG_FATAL = 3;

/// Map glog severity int to spdlog level.
inline spdlog::level::level_enum GlogLevelToSpdlog(int severity) {
    switch (severity) {
        case GLOG_INFO:
            return spdlog::level::info;
        case GLOG_WARNING:
            return spdlog::level::warn;
        case GLOG_ERROR:
            return spdlog::level::err;
        case GLOG_FATAL:
            return spdlog::level::critical;
        default:
            return spdlog::level::info;
    }
}

// ---------------------------------------------------------------------------
// LogStream — RAII stream adapter for spdlog
//
// Collects streamed values via operator<< and emits a single log message
// when the object is destroyed (end of full-expression).
// ---------------------------------------------------------------------------
class LogStream {
   public:
    explicit LogStream(spdlog::level::level_enum level, bool enabled = true)
        : level_(level), enabled_(enabled) {}

    ~LogStream() {
        if (enabled_) {
            spdlog::default_logger_raw()->log(level_, "{}", oss_.str());
            if (level_ == spdlog::level::critical) {
                spdlog::default_logger_raw()->flush();
                std::abort();
            }
        }
    }

    // Non-copyable, movable
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStream(LogStream&& other) noexcept
        : level_(other.level_),
          enabled_(other.enabled_),
          oss_(std::move(other.oss_)) {
        other.enabled_ = false;
    }

    template <typename T>
    LogStream& operator<<(const T& val) {
        if (enabled_) {
            oss_ << val;
        }
        return *this;
    }

    // Support for std::endl and other stream manipulators
    LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        if (enabled_) {
            manip(oss_);
        }
        return *this;
    }

   private:
    spdlog::level::level_enum level_;
    bool enabled_;
    std::ostringstream oss_;
};

// ---------------------------------------------------------------------------
// PLogStream — like LogStream but appends errno information
// ---------------------------------------------------------------------------
class PLogStream {
   public:
    explicit PLogStream(spdlog::level::level_enum level, int saved_errno,
                        bool enabled = true)
        : level_(level), saved_errno_(saved_errno), enabled_(enabled) {}

    ~PLogStream() {
        if (enabled_) {
            oss_ << ": " << std::strerror(saved_errno_)
                 << " [errno=" << saved_errno_ << "]";
            spdlog::default_logger_raw()->log(level_, "{}", oss_.str());
            if (level_ == spdlog::level::critical) {
                spdlog::default_logger_raw()->flush();
                std::abort();
            }
        }
    }

    PLogStream(const PLogStream&) = delete;
    PLogStream& operator=(const PLogStream&) = delete;
    PLogStream(PLogStream&& other) noexcept
        : level_(other.level_),
          saved_errno_(other.saved_errno_),
          enabled_(other.enabled_),
          oss_(std::move(other.oss_)) {
        other.enabled_ = false;
    }

    template <typename T>
    PLogStream& operator<<(const T& val) {
        if (enabled_) {
            oss_ << val;
        }
        return *this;
    }

    PLogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        if (enabled_) {
            manip(oss_);
        }
        return *this;
    }

   private:
    spdlog::level::level_enum level_;
    int saved_errno_;
    bool enabled_;
    std::ostringstream oss_;
};

// ---------------------------------------------------------------------------
// VlogIsOn — check if verbose logging at level n is enabled
// ---------------------------------------------------------------------------
inline bool VlogIsOn(int n) { return n <= GetVerboseLevel(); }

}  // namespace logging
}  // namespace mooncake

// ===========================================================================
// Public macros — drop-in replacements for glog
// ===========================================================================

// Severity name constants (used by LOG(severity) macros)
#define INFO    ::mooncake::logging::GLOG_INFO
#define WARNING ::mooncake::logging::GLOG_WARNING
#define ERROR   ::mooncake::logging::GLOG_ERROR
#define FATAL   ::mooncake::logging::GLOG_FATAL

// --- LOG(severity) ---
#define LOG(severity) \
    ::mooncake::logging::LogStream(::mooncake::logging::GlogLevelToSpdlog(severity))

// --- DLOG(severity) — only active in debug builds ---
#ifdef NDEBUG
#define DLOG(severity) \
    ::mooncake::logging::LogStream(::mooncake::logging::GlogLevelToSpdlog(severity), false)
#else
#define DLOG(severity) LOG(severity)
#endif

// --- VLOG(n) — verbose logging ---
#define VLOG(n) \
    if (::mooncake::logging::VlogIsOn(n)) \
        ::mooncake::logging::LogStream(spdlog::level::debug)

// --- VLOG_IS_ON(n) ---
#define VLOG_IS_ON(n) (::mooncake::logging::VlogIsOn(n))

// --- PLOG(severity) — appends strerror(errno) ---
#define PLOG(severity) \
    ::mooncake::logging::PLogStream(                         \
        ::mooncake::logging::GlogLevelToSpdlog(severity), errno)

// --- LOG_EVERY_N(severity, n) — log once every n times ---
#define LOG_EVERY_N(severity, n)                              \
    static std::atomic<int> LOG_EVERY_N_COUNTER_##__LINE__{0}; \
    if (LOG_EVERY_N_COUNTER_##__LINE__.fetch_add(1, std::memory_order_relaxed) % (n) == 0) \
        LOG(severity)

// --- LOG_IF(severity, condition) ---
#define LOG_IF(severity, condition) \
    if (condition) LOG(severity)

// --- LOG_FIRST_N(severity, n) — log only the first n times ---
#define LOG_FIRST_N(severity, n)                              \
    static std::atomic<int> LOG_FIRST_N_COUNTER_##__LINE__{0}; \
    if (LOG_FIRST_N_COUNTER_##__LINE__.fetch_add(1, std::memory_order_relaxed) < (n)) \
        LOG(severity)
