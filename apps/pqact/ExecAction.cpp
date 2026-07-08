#include "ExecAction.h"
#include "ProcessUtil.h"
#include "ProcessManager.h"
#include "PrivilegeManager.h"
#include "Log.h"
#include "PqactContext.h"
#include <unistd.h>
#include <csignal>
#include <sys/wait.h>
#include <vector>

namespace rdm {
namespace pqact {

int ExecAction::Execute(const Product& prod, const std::vector<std::string>& args, const void* xprod, size_t xlen) {
    if (args.empty()) {
        LogError("No command specified for EXEC action");
        return -1;
    }

    bool waitOnChild = (args[0] == "-wait");
    size_t cmdIndex = waitOnChild ? 1 : 0;
    if (cmdIndex >= args.size()) return -1;

    // Build the argv array in the parent so we can pass it to ForkAndExec
    std::vector<char*> c_args;
    for (size_t i = cmdIndex; i < args.size(); ++i) {
        c_args.push_back(const_cast<char*>(args[i].c_str()));
    }
    c_args.push_back(nullptr);

    os::ExecParams params;
    params.argv = c_args.data();
    params.setPgid = true;
    params.resetSignals = true;

    pid_t pid = os::ForkAndExec(params);
    if (pid == -1) {
        LogSyserr("Couldn't fork EXEC process");
        return -1;
    }

    std::string cmdStr = "pqact EXEC: ";
    for (size_t i = cmdIndex; i < args.size(); ++i) {
        if (i > cmdIndex) cmdStr += " ";
        cmdStr += args[i];
    }

    context_.procMgr.Add(pid, cmdStr);

    if (waitOnChild) {
        LogDebug("exec -wait {}[{}]", args[cmdIndex], pid);
        context_.procMgr.Reap(pid, 0);
    } else {
        LogDebug("exec {}[{}]", args[cmdIndex], pid);
    }

    return 0;
}

}
}
