#include "ProcessManager.h"
#include "ProcessUtil.h"
#include "PrivilegeManager.h"
#include "Log.h"
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>

namespace rdm {

pid_t ProcessManager::SpawnExec(const ExecRule& rule) {
    os::ExecParams params;
    params.argv = rule.command.getArgv();
    params.resetSignals = true;

    pid_t pid = os::ForkAndExec(params);
    if (pid == -1) {
        return -1;
    }

    std::string cmdStr;
    for (size_t i = 0; i < rule.command.getArgc(); ++i) {
        if (i > 0) cmdStr += " ";
        cmdStr += rule.command.getArgv()[i];
    }
    
    Add(pid, cmdStr);
    return pid;
}

pid_t ProcessManager::SpawnRequester(const std::string& host, std::function<void()> runFunc) {
    pid_t pid = os::ldmFork();
    if (pid == -1) return -1;

    if (pid == 0) {
        // Drop privileges for the requester thread safely
        PrivilegeManager::Instance().PermanentlyDropPrivileges();
        runFunc();
        _exit(0);
    }

    Add(pid, "REQUEST " + host);
    return pid;
}

bool ProcessManager::Add(pid_t pid, const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = activeProcesses_.emplace(pid, description);
    return result.second;
}

bool ProcessManager::Remove(pid_t pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeProcesses_.erase(pid) > 0;
}

bool ProcessManager::Contains(pid_t pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeProcesses_.find(pid) != activeProcesses_.end();
}

size_t ProcessManager::Count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeProcesses_.size();
}

std::string ProcessManager::GetCommand(pid_t pid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = activeProcesses_.find(pid);
    if (it != activeProcesses_.end()) {
        return it->second;
    }
    return "Unknown Process";
}

pid_t ProcessManager::Reap(pid_t pid, int options, int* outStatus) {
    int status = 0;
    pid_t wpid;

    // Retry waitpid if it is interrupted by a signal (like SIGCHLD)
    do {
        wpid = waitpid(pid, &status, options);
    } while (wpid == -1 && errno == EINTR);

    if (wpid == -1) {
        if (!(errno == ECHILD && pid == -1)) {
            LogSyserr("waitpid");
        }
        return -1;
    }

    if (wpid > 0) {
        std::string cmdStr = GetCommand(wpid);
        Remove(wpid);
        if (outStatus) *outStatus = status;

        if (WIFSTOPPED(status)) {
            LogNotice("Child {} stopped by signal {}: {}",
                       static_cast<long>(wpid), WSTOPSIG(status), cmdStr);
        } else if (WIFSIGNALED(status)) {
            LogNotice("Child {} terminated by signal {}: {}",
                       static_cast<long>(wpid), WTERMSIG(status), cmdStr);
        } else if (WIFEXITED(status)) {
            int exitStatus = WEXITSTATUS(status);
            if (exitStatus == 0) {
                LogInfo("Child {} exited successfully: {}", static_cast<long>(wpid), cmdStr);
            } else {
                LogNotice("Child {} exited with status {}: {}", static_cast<long>(wpid), exitStatus, cmdStr);
            }
        }
    }
    return wpid;
}

}
