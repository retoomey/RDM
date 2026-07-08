#pragma once

#include <sys/types.h>

namespace rdm {
namespace os {
/**
 * Configuration parameters for spawning a new process via ForkAndExec.
 */
struct ExecParams {
  char * const * argv;                 // Null-terminated array of arguments (argv[0] is the command)
  bool           setPgid      = false; // If true, sets the child as a new process group leader
  int            stdinFd      = -1;    // If >= 0, dups this fd to STDIN_FILENO
  int            stdoutFd     = -1;    // If >= 0, dups this fd to STDOUT_FILENO
  int            stderrFd     = -1;    // If >= 0, dups this fd to STDERR_FILENO
  bool           resetSignals = true;  // If true, resets common signals to SIG_DFL or SIG_IGN
};

/**
 * @brief Fork a child process cleanly, safely closing LDM registry resources
 * and isolating logging states.
 * @return PID of the child process, 0 if child, -1 on error.
 */
pid_t
ldmFork();

/**
 * @brief Ensures that a file descriptor is closed automatically on exec().
 * @param fd The file descriptor.
 * @return 0 on success, -1 on failure.
 */
int
ensureCloseOnExec(int fd);

/**
 * @brief Opens /dev/null on a given file descriptor if it is currently closed.
 * @param fileno The file descriptor number (e.g., STDIN_FILENO).
 * @param flags The open flags (e.g., O_RDWR).
 * @return 0 on success, -1 on failure.
 */
int
openOnDevNullIfClosed(int fileno, int flags);

/**
 * Safely forks and executes a new process, handling privilege dropping,
 * signal resetting, and file descriptor redirection.
 * * @param params The configuration parameters for the execution.
 * @return The PID of the child process, or -1 on failure.
 */
pid_t
ForkAndExec(const ExecParams& params);
} // namespace os
}
