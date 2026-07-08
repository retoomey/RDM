#include "PrivilegeManager.h"
#include "Log.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>

#ifndef NDEBUG
#ifdef HAS_SYS_PRCTL_H
    #include <sys/prctl.h>
#endif
#endif

namespace rdm {

PrivilegeManager::PrivilegeManager() {
    realUid_ = getuid();    // Who actually executed the binary
    realGid_ = getgid();    // Their primary group
    uid_t euid = geteuid(); // Who the kernel currently thinks we are
    
    bool hasRootCapabilities = (euid == 0);

    isPrivileged_ = (hasRootCapabilities && realUid_ != 0);

    // --- Debug-Only Core Dump Enabler ---
#ifndef NDEBUG
#ifdef HAS_SYS_PRCTL_H
    // If the binary is setuid, the Linux kernel disables core dumps by default to prevent 
    // memory snooping. We re-enable them here exclusively for Debug builds.
    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
        LogSyserr("Couldn't give process the ability to create a core file (prctl failed)");
    } else {
        LogDebug("Core dumps explicitly enabled via prctl(PR_SET_DUMPABLE) for this debug build.");
    }
#endif
#endif
    // ------------------------------------

    if (isPrivileged_) {
        LogInfo("PrivilegeManager initialized via setuid wrapper. Swapping enabled.");
    } else if (hasRootCapabilities) {
        LogInfo("PrivilegeManager initialized directly as pure root. Swapping disabled.");
    } else {
        LogDebug("PrivilegeManager initialized as unprivileged user {}. Swapping disabled.", realUid_);
    }
}

void PrivilegeManager::EnforcePortPolicy(int port) {
    bool isPrivilegedPort = (port > 0 && port < 1024);
    uid_t euid = isPrivileged_ ? 0 : realUid_; // If we can swap, we effectively have root

    if (isPrivilegedPort) {
        // CASE A: User wants a low port (e.g., 388).
        if (euid != 0) {
            // They asked for port 388 but are running as a standard user without setuid. Fail fast.
            LogError("CRITICAL CONFIGURATION ERROR: Port {} is a privileged port, "
                      "but this process lacks root capabilities. "
                      "Ensure the binary is owned by root and has the setuid bit enabled "
                      "(chmod u+s), or run on an unprivileged port. Exiting.", 
                      port);
            std::exit(1); 
        }
        LogDebug("Port {} requires root. Retaining privilege swapping capabilities.", port);
        
    } else {
        // CASE B: User wants a high port (e.g., 3880) or is a client utility (port = 0).
        if (isPrivileged_) {
            // Principle of Least Privilege: They have setuid root, but don't actually need it 
            // for this port. Permanently destroy the root capabilities to secure the process.
            LogInfo("Port {} is unprivileged. Permanently shedding unused root capabilities for security.", port);
            
            // We must briefly raise to root just to have the permission to permanently drop it 
            // across all IDs (Real, Effective, Saved).
            if (seteuid(0) == 0) {
                PermanentlyDropPrivileges();
            } else {
                LogSyserr("Failed to raise privileges to perform permanent drop.");
            }
        }
    }
}

bool PrivilegeManager::LowerPrivileges() {
    if (!isPrivileged_) return true; // Safe no-op for unprivileged/pure root deployments

    // Set Effective User/Group ID back to the launching user, keeping Saved ID as root
    if (setegid(realGid_) != 0) {
        LogSyserr("Failed to temporarily lower effective group privileges");
        return false;
    }
    if (seteuid(realUid_) != 0) {
        LogSyserr("Failed to temporarily lower effective user privileges");
        return false;
    }
    return true;
}

bool PrivilegeManager::RaisePrivileges() {
    if (!isPrivileged_) return true; // Safe no-op

    // Restore Effective User/Group ID back to root (0) using our Saved ID capability
    if (seteuid(0) != 0) {
        LogSyserr("Failed to restore root user privileges");
        return false;
    }
    if (setegid(0) != 0) {
        LogSyserr("Failed to restore root group privileges");
        return false;
    }
    return true;
}

bool PrivilegeManager::PermanentlyDropPrivileges() {
    if (!isPrivileged_) return true; // Safe no-op

    // Permanently overwrite Real, Effective, and Saved user/group IDs to the unprivileged user.
    // Once this executes, there is no going back to root.
    if (setgid(realGid_) != 0) {
        LogSyserr("Failed to permanently drop group privileges");
        return false;
    }
    if (setuid(realUid_) != 0) {
        LogSyserr("Failed to permanently drop user privileges");
        return false;
    }
    
    // Update internal state so subsequent calls to Raise/Lower become harmless no-ops.
    isPrivileged_ = false;
    return true;
}

}
