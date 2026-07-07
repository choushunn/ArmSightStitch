#pragma once

#include <memory>
#include <string>
#include <mutex>
#include <iostream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace infra {

class LogManager {
public:
    static void init(const std::string& logFile = "",
                     spdlog::level::level_enum level = spdlog::level::debug) {
        std::call_once(init_flag_, [&]() {
            std::vector<spdlog::sink_ptr> sinks;
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

            if (!logFile.empty()) {
                try {
                    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true));
                } catch (const spdlog::spdlog_ex& e) {
                    std::cerr << "Warning: Could not create log file sink ("
                              << logFile << "): " << e.what()
                              << " — falling back to console only" << std::endl;
                }
            }

            auto logger = std::make_shared<spdlog::logger>("arm", sinks.begin(), sinks.end());
            logger->set_level(level);
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
            set_default_logger(logger);

            SPDLOG_INFO("LogManager initialized (file: {})",
                        logFile.empty() ? "console only" : logFile);
        });
    }

    static void shutdown() {
        spdlog::shutdown();
        // std::call_once cannot be reset; re-init after shutdown is not supported
    }

private:
    static std::once_flag init_flag_;
};

inline std::once_flag LogManager::init_flag_;

} // namespace infra
