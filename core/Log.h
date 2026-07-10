#pragma once

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <memory>

// Include fmt for compile-time string evaluation, but absolutely NO spdlog headers.
#include <spdlog/fmt/bundled/core.h>

namespace spdlog { class logger; }

namespace rdm {

typedef enum {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO,
  LOG_LEVEL_NOTICE,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_FATAL,
  LOG_LEVEL_COUNT
} log_level_t;

// Forward declare the spdlog logger so we can accept the shared_ptr from the host
namespace spdlog { class logger; }

// --- Injection API for Host Applications (like RAPIO) ---
void LogInjectSpdlog(std::shared_ptr<spdlog::logger> external_logger);

// --- Core API ---
bool log_is_level_enabled(log_level_t level);
int log_set_level(log_level_t level);
log_level_t log_get_level(void);
void log_roll_level(void);
const char * log_get_default_destination(void);
const char * log_get_default_daemon_destination(void);
int LogInitialize(const char * id);
void LogShutdown(void);
int log_set_destination(const char * dest);
int log_set_upstream_id(const char * hostId, const bool isFeeder);
int log_set_id(const char * id);
bool log_stderr_is_open(void);

// --- Internal Routing ---
void log_route_message(log_level_t level, const std::string& msg);

} // namespace ldm

#define log_is_enabled_debug   rdm::log_is_level_enabled(rdm::LOG_LEVEL_DEBUG)
#define log_is_enabled_info    rdm::log_is_level_enabled(rdm::LOG_LEVEL_INFO)
#define log_is_enabled_notice  rdm::log_is_level_enabled(rdm::LOG_LEVEL_NOTICE)
#define log_is_enabled_warning rdm::log_is_level_enabled(rdm::LOG_LEVEL_WARNING)
#define log_is_enabled_error   rdm::log_is_level_enabled(rdm::LOG_LEVEL_ERROR)
#define log_is_enabled_fatal   rdm::log_is_level_enabled(rdm::LOG_LEVEL_FATAL)

namespace rdm {

struct ErrnoPreserver {
  int saved_errno;
  ErrnoPreserver() : saved_errno(errno) { }
  ~ErrnoPreserver() { errno = saved_errno; }
};

// --- Logging Macros ---

template <typename ... Args>
inline void LogDebug(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_debug) return;
  log_route_message(LOG_LEVEL_DEBUG, fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename ... Args>
inline void LogInfo(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_info) return;
  log_route_message(LOG_LEVEL_INFO, fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename ... Args>
inline void LogNotice(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_notice) return;
  log_route_message(LOG_LEVEL_NOTICE, "[NOTICE] " + fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename ... Args>
inline void LogWarning(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_warning) return;
  log_route_message(LOG_LEVEL_WARNING, fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename ... Args>
inline void LogError(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_error) return;
  log_route_message(LOG_LEVEL_ERROR, fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename ... Args>
inline void LogFatal(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_fatal) return;
  log_route_message(LOG_LEVEL_FATAL, fmt::format(fmt_str, std::forward<Args>(args)...));
}

template <typename ... Args>
inline void LogSyserr(fmt::format_string<Args...> fmt_str, Args&&... args) {
  ErrnoPreserver ep;
  if (!log_is_enabled_error) return;
  std::string user_msg = fmt::format(fmt_str, std::forward<Args>(args)...);
  log_route_message(LOG_LEVEL_ERROR, fmt::format("{} : {}", user_msg, std::strerror(ep.saved_errno)));
}

} // namespace rdm

#ifdef NDEBUG
# define log_assert(expr) ((void) 0)
#else
# define log_assert(expr) \
  do { \
    if (!(expr)) { \
      rdm::LogFatal("Assertion failure: {}", #expr); \
      std::abort(); \
    } \
  } while (0)
#endif
