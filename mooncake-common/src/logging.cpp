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

#include "logging.h"

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <iostream>
#include <vector>

namespace mooncake {
namespace logging {

static std::atomic<int> g_verbose_level{0};

void SetVerboseLevel(int level) {
    g_verbose_level.store(level, std::memory_order_relaxed);
}

int GetVerboseLevel() {
    return g_verbose_level.load(std::memory_order_relaxed);
}

void InitMooncakeLogging(const char* program_name,
                         const LoggingConfig& config) {
    try {
        // Extract base name from program path
        std::string prog_name = "mooncake";
        if (program_name && program_name[0] != '\0') {
            std::filesystem::path p(program_name);
            prog_name = p.filename().string();
        }

        // Build log file path: <log_dir>/<program_name>.log
        std::filesystem::path log_dir(config.log_dir);
        std::filesystem::create_directories(log_dir);
        std::string log_path =
            (log_dir / (prog_name + ".log")).string();

        // Create sinks
        std::vector<spdlog::sink_ptr> sinks;

        // Rotating file sink
        auto rotating_sink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_path, config.max_file_size, config.max_files);
        sinks.push_back(rotating_sink);

        // Stderr sink (optional)
        if (config.log_to_stderr) {
            auto stderr_sink =
                std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
            sinks.push_back(stderr_sink);
        }

        // Initialize async thread pool
        spdlog::init_thread_pool(config.async_queue_size,
                                 config.async_thread_count);

        // Create async logger with all sinks
        auto logger = std::make_shared<spdlog::async_logger>(
            prog_name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);

        // Set log format: [YYYY-MM-DD HH:MM:SS.mmm] [level] [thread] message
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [t:%t] %v");
        logger->set_level(config.min_level);
        logger->flush_on(spdlog::level::warn);

        // Set as default logger
        spdlog::set_default_logger(logger);

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Mooncake logging initialization failed: " << ex.what()
                  << std::endl;
        // Fallback: create a simple stderr logger
        auto fallback = spdlog::stderr_color_mt("mooncake_fallback");
        spdlog::set_default_logger(fallback);
    }
}

void ShutdownMooncakeLogging() {
    spdlog::default_logger_raw()->flush();
    spdlog::shutdown();
}

}  // namespace logging
}  // namespace mooncake
