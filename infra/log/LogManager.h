#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace infra {

class LogManager {
public:
    static void init(const std::string& logFile = "",
                     spdlog::level::level_enum level = spdlog::level::debug) {
        if (initialized_) return;

        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

        if (!logFile.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true));
        }

        auto logger = std::make_shared<spdlog::logger>("arm", sinks.begin(), sinks.end());
        logger->set_level(level);
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        set_default_logger(logger);

        initialized_ = true;
        SPDLOG_INFO("LogManager initialized (file: {})", logFile.empty() ? "console only" : logFile);
    }

    static void shutdown() {
        spdlog::shutdown();
        initialized_ = false;
    }

private:
    static bool initialized_;
};

inline bool LogManager::initialized_ = false;

} // namespace infra
