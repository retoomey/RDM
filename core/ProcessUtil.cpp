/**
 * @file ProcessUtil.cpp
 * @brief Modernized OS and Process management utilities.
 */
#include "ProcessUtil.h"
#include "config.h"
#include "Log.h"
#include "Registry.h"
#include "PrivilegeManager.h"
#include <cerrno>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <sys/types.h>

namespace rdm {
namespace os {

pid_t ldmFork() {
    registry::close();
    pid_t pid = fork();
    if (0 == pid) {
    } else if (-1 == pid) {
        LogSyserr("Couldn't fork a child process");
    }
    return pid;
}

int ensureCloseOnExec(int fd) {
    int status = fcntl(fd, F_GETFD);
    if (status == -1) {
        LogSyserr("Couldn't get file descriptor flags: fd={}", fd);
    } else if (status & FD_CLOEXEC) {
        status = 0;
    } else {
        status = fcntl(fd, F_SETFD, status | FD_CLOEXEC);
        if (status == -1) {
            LogSyserr("Couldn't set file descriptor to close-on-exec: fd={}", fd);
        } else {
            status = 0;
        }
    }
    return status;
}

int openOnDevNullIfClosed(int fileno, int flags) {
    int status;
    if (fcntl(fileno, F_GETFD) >= 0) {
        status = 0;
    } else {
        status = -1;
        int fd = open("/dev/null", flags);
        if (fd < 0) {
            LogSyserr("Couldn't open /dev/null: flags={:#X}", flags);
        } else if (fd == fileno) {
            status = 0;
        } else {
            if (dup2(fd, fileno) < 0) {
                LogSyserr("dup2() failure: fd={}, fileno={}", fd, fileno);
            } else {
                status = 0;
            }
            (void)close(fd);
        }
    }
    return status;
}

pid_t ForkAndExec(const ExecParams& params) {
    if (!params.argv || !params.argv[0]) {
        LogError("ForkAndExec: Invalid arguments provided");
        return -1;
    }

    pid_t pid = ldmFork();
    if (pid == -1) {
        LogSyserr("Couldn't fork process for \"{}\"", params.argv[0]);
        return -1;
    }

    if (pid == 0) {
        // --- CHILD PROCESS ---

        if (params.setPgid) {
            if (setpgid(0, 0) == -1) {
                LogWarning("Couldn't make process group leader for \"{}\"", params.argv[0]);
            }
        }

        if (params.resetSignals) {
            struct sigaction sigact;
            sigact.sa_flags = 0;
            sigemptyset(&sigact.sa_mask);
            
            // Standardize core signals to default
            sigact.sa_handler = SIG_DFL;
            sigaction(SIGPIPE, &sigact, nullptr);
            sigaction(SIGCHLD, &sigact, nullptr);
            sigaction(SIGALRM, &sigact, nullptr);
            sigaction(SIGINT,  &sigact, nullptr);
            sigaction(SIGTERM, &sigact, nullptr);
            
            // Ignore custom LDM user signals so they don't bleed into standard utilities
            sigact.sa_handler = SIG_IGN;
            sigaction(SIGUSR1, &sigact, nullptr);
            sigaction(SIGUSR2, &sigact, nullptr);
        }

        // Handle file descriptor redirection
        if (params.stdinFd >= 0 && params.stdinFd != STDIN_FILENO) {
            dup2(params.stdinFd, STDIN_FILENO);
            close(params.stdinFd);
        }
        if (params.stdoutFd >= 0 && params.stdoutFd != STDOUT_FILENO) {
            dup2(params.stdoutFd, STDOUT_FILENO);
            close(params.stdoutFd);
        }
        if (params.stderrFd >= 0 && params.stderrFd != STDERR_FILENO) {
            dup2(params.stderrFd, STDERR_FILENO);
            close(params.stderrFd);
        }

        // Crucial security step: permanently drop privileges before loading the new binary
        PrivilegeManager::Instance().PermanentlyDropPrivileges();

        execvp(params.argv[0], params.argv);

        // If execvp returns, it failed to swap the process image
        LogSyserr("Couldn't execute utility \"{}\"", params.argv[0]);
        _exit(127);
    }

    // --- PARENT PROCESS ---
    return pid;
}

}
}
