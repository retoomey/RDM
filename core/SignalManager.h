#pragma once
#include <functional>
#include <csignal>

namespace rdm {
class SignalManager {
public:
  // Establishes the standard LDM signal baseline:
  // - SIGPIPE is ignored
  // - SIGUSR1 calls log_refresh()
  // - SIGUSR2 calls log_roll_level()
  // - SIGINT, SIGTERM set `done = 1`
  // - SIGCONT interrupts select()
  static void
  Initialize();

  // Register a callback for graceful shutdown (SIGINT/SIGTERM).
  // This executes immediately after `done` is set to 1.
  static void
  SetShutdownHook(std::function<void()> hook);

  // Register a callback for configuration reload (SIGHUP).
  // If not set, SIGHUP defaults to calling log_refresh().
  static void
  SetHangupHook(std::function<void()> hook);

  // Explicitly ignore a specific signal (useful for app-specific quirks like SIGXFSZ)
  static void
  Ignore(int signum);

  // Modernized state checking
  static bool
  IsDone();
  static int
  ExitIfDone(int status);

  static void
  TriggerShutdown();
};
}
