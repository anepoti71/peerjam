#include "peerjam/common/logger.hpp"

#include <cstdio>
#include <iostream>

namespace peerjam {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

LogLevel parse_log_level(const std::string& text) {
    if (text == "debug") {
        return LogLevel::Debug;
    }
    if (text == "info") {
        return LogLevel::Info;
    }
    if (text == "error") {
        return LogLevel::Error;
    }
    return LogLevel::Off;
}

void Logger::configure(const LoggingOptions& options) {
    std::lock_guard lock(mutex_);
    enabled_.store(options.enabled);
    configured_level_.store(static_cast<int>(options.enabled ? parse_log_level(options.level) : LogLevel::Off));
    sink_ = options.sink.empty() ? "stderr" : options.sink;
    file_path_ = options.file_path;
    if (file_handle_) {
        std::fclose(static_cast<std::FILE*>(file_handle_));
        file_handle_ = nullptr;
    }
    if (enabled_ && sink_ == "file" && !file_path_.empty()) {
        file_handle_ = std::fopen(file_path_.c_str(), "a");
    }
}

bool Logger::should_log(LogLevel level) const {
    return enabled_.load() && static_cast<int>(level) <= configured_level_.load();
}

void Logger::log(LogLevel level, const std::string& message, const char* file, int line) {
    std::lock_guard lock(mutex_);
    if (!enabled_.load() || static_cast<int>(level) > configured_level_.load()) {
        return;
    }

    const char* label = "OFF";
    switch (level) {
        case LogLevel::Error:
            label = "ERROR";
            break;
        case LogLevel::Info:
            label = "INFO";
            break;
        case LogLevel::Debug:
            label = "DEBUG";
            break;
        case LogLevel::Off:
            label = "OFF";
            break;
    }

    if (sink_ == "file" && file_handle_) {
        std::fprintf(static_cast<std::FILE*>(file_handle_), "[%s] %s:%d %s\n", label, file, line, message.c_str());
        std::fflush(static_cast<std::FILE*>(file_handle_));
    } else {
        std::fprintf(stderr, "[%s] %s:%d %s\n", label, file, line, message.c_str());
        std::fflush(stderr);
    }
}

}  // namespace peerjam
