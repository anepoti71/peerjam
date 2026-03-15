#pragma once

#include "peerjam/logic/control/peer_types.hpp"

#include <atomic>
#include <mutex>
#include <sstream>
#include <string>

namespace peerjam {

enum class LogLevel {
    Off = 0,
    Error = 1,
    Info = 2,
    Debug = 3,
};

class Logger {
  public:
    static Logger& instance();

    void configure(const LoggingOptions& options);
    bool should_log(LogLevel level) const;
    void log(LogLevel level, const std::string& message, const char* file, int line);

  private:
    Logger() = default;

    std::atomic<int> configured_level_ {static_cast<int>(LogLevel::Off)};
    std::atomic<bool> enabled_ {false};
    std::string sink_ {"stderr"};
    std::string file_path_;
    mutable std::mutex mutex_;
    void* file_handle_ {nullptr};
};

LogLevel parse_log_level(const std::string& text);

}  // namespace peerjam

#define PJ_LOG(level, expr) \
    do { \
        auto& _pj_logger = ::peerjam::Logger::instance(); \
        if (_pj_logger.should_log(::peerjam::LogLevel::level)) { \
            std::ostringstream _pj_os; \
            _pj_os << expr; \
            _pj_logger.log(::peerjam::LogLevel::level, _pj_os.str(), __FILE__, __LINE__); \
        } \
    } while (0)
