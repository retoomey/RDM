#include "Log.h"

// It is entirely safe to include spdlog and its sinks here in the translation unit
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/syslog_sink.h>

#include <string>
#include <libgen.h>
#include <cstring>

namespace rdm {

static log_level_t g_current_log_level = LOG_LEVEL_NOTICE;
static std::string g_progname = "ldm";
static bool g_is_injected = false;

// Fast translation array mapping custom LDM levels to native spdlog levels
static const ::spdlog::level::level_enum g_spdlog_levels[] = {
    ::spdlog::level::debug,    // LOG_LEVEL_DEBUG
    ::spdlog::level::info,     // LOG_LEVEL_INFO
    ::spdlog::level::warn,     // LOG_LEVEL_NOTICE (Maps to warn)
    ::spdlog::level::warn,     // LOG_LEVEL_WARNING
    ::spdlog::level::err,      // LOG_LEVEL_ERROR
    ::spdlog::level::critical  // LOG_LEVEL_FATAL
};

// --- Injection Logic ---

void LogInjectSpdlog(std::shared_ptr<::spdlog::logger> external_logger) {
    if (external_logger) {
        ::spdlog::set_default_logger(external_logger);
        g_is_injected = true;
    }
}

// --- Routing & Gating Logic ---

bool log_is_level_enabled(log_level_t level) {
    auto logger = ::spdlog::default_logger();
    if (!logger || level < 0 || level >= LOG_LEVEL_COUNT) return false;

    // Delegate directly to the active ::spdlog logger using the translated level
    return logger->should_log(g_spdlog_levels[level]);
}

void log_route_message(log_level_t level, const std::string& msg) {
    if (level < 0 || level >= LOG_LEVEL_COUNT) return;
    
    auto logger = ::spdlog::default_logger();
    if (logger) {
        logger->log(g_spdlog_levels[level], msg);
    }
}

// --- Standalone Initialization & Fallbacks ---

int LogInitialize(const char* id) {
    if (id != nullptr) {
        char* id_copy = strdup(id);
        g_progname = basename(id_copy);
        free(id_copy);
    }
    
    // If a host application injected a logger, do not overwrite it with standalone sinks
    if (!g_is_injected) {
        return log_set_destination(log_get_default_destination());
    }
    return 0;
}

int log_set_destination(const char* dest) {
    if (g_is_injected) return 0; // Ignore standalone destination changes if injected
    if (dest == nullptr) return -1;
    
    std::string d(dest);
    try {
        ::spdlog::drop_all();
        std::shared_ptr<::spdlog::logger> logger;
        
        if (d == "-") {
            logger = ::spdlog::stderr_color_mt(g_progname);
        } else if (d.empty()) {
            logger = ::spdlog::syslog_logger_mt(g_progname, g_progname, LOG_PID, LOG_LOCAL0);
        } else {
            logger = ::spdlog::basic_logger_mt(g_progname, d);
        }
        
        ::spdlog::set_default_logger(logger);
        ::spdlog::flush_on(::spdlog::level::info);
        ::spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P] [%^%l%$] [" + g_progname + "] %v");
        
        // Reapply the standalone log level
        log_set_level(g_current_log_level);
        return 0;
    } catch (const ::spdlog::spdlog_ex& ex) {
        return -1;
    }
}

int log_set_level(log_level_t level) {
    g_current_log_level = level;
    if (level >= 0 && level < LOG_LEVEL_COUNT) {
        ::spdlog::set_level(g_spdlog_levels[level]);
    }
    return 0;
}

log_level_t log_get_level(void) {
    return g_current_log_level;
}

void LogShutdown(void) {
    // Only shutdown the logger if we own it
    if (!g_is_injected) {
        ::spdlog::shutdown();
    }
}

void log_roll_level(void) {
    if (g_current_log_level == LOG_LEVEL_DEBUG) {
        log_set_level(LOG_LEVEL_INFO);
        LogNotice("Logging level rolled to INFO via SIGUSR2");
    } else {
        log_set_level(LOG_LEVEL_DEBUG);
        LogNotice("Logging level rolled to DEBUG via SIGUSR2");
    }
}

const char* log_get_default_destination(void) { return "-"; }
const char* log_get_default_daemon_destination(void) { return "ldmd.log"; }
int log_set_upstream_id(const char* hostId, const bool isFeeder) { return 0; }
int log_set_id(const char* id) { return 0; }
bool log_stderr_is_open(void) { return true; }

} // namespace rdm
