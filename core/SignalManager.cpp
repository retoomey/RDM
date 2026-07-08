#include "SignalManager.h"
#include "Log.h"
#include <csignal>
#include <utility>

namespace rdm {

static std::function<void()> g_shutdownHook = nullptr;
static std::function<void()> g_hangupHook = nullptr;
static volatile std::sig_atomic_t g_done = 0;
static volatile std::sig_atomic_t g_roll_logs = 0;

// NEW: Track SIGHUP requests safely in a volatile sig_atomic_t flag.
static volatile std::sig_atomic_t g_hup_requested = 0;

static void GlobalSignalHandler(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            g_done = 1;
            break;
        case SIGHUP:
            // NEW: Just set the flag. Execution happens safely in the main thread polling IsDone().
            g_hup_requested = 1;
            break;
        case SIGUSR1:
            break;
        case SIGUSR2:
            g_roll_logs = 1;
            break;
        case SIGALRM:
        case SIGCHLD:
        case SIGCONT:
            break;
    }
}

void SignalManager::Initialize() {
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sigact, nullptr);

    sigact.sa_handler = GlobalSignalHandler;
    sigaction(SIGINT, &sigact, nullptr);
    sigaction(SIGTERM, &sigact, nullptr);
    sigaction(SIGALRM, &sigact, nullptr);
    sigaction(SIGCONT, &sigact, nullptr);

    sigact.sa_flags |= SA_RESTART;
    sigaction(SIGHUP, &sigact, nullptr);
    sigaction(SIGUSR1, &sigact, nullptr);
    sigaction(SIGUSR2, &sigact, nullptr);
    sigaction(SIGCHLD, &sigact, nullptr);

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE);
    sigaddset(&sigset, SIGINT);
    sigaddset(&sigset, SIGTERM);
    sigaddset(&sigset, SIGALRM);
    sigaddset(&sigset, SIGHUP);
    sigaddset(&sigset, SIGUSR1);
    sigaddset(&sigset, SIGUSR2);
    sigaddset(&sigset, SIGCHLD);
    sigaddset(&sigset, SIGCONT);
    sigprocmask(SIG_UNBLOCK, &sigset, nullptr);
}

void SignalManager::SetShutdownHook(std::function<void()> hook) {
    g_shutdownHook = std::move(hook);
}

void SignalManager::SetHangupHook(std::function<void()> hook) {
    g_hangupHook = std::move(hook);
}

void SignalManager::Ignore(int signum) {
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = SIG_IGN;
    sigaction(signum, &sigact, nullptr);

    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, signum);
    sigprocmask(SIG_UNBLOCK, &sigset, nullptr);
}

bool SignalManager::IsDone() {
    if (g_roll_logs) {
        log_roll_level();
        g_roll_logs = 0;
    }

    // Safely execute the hangup hook in the main thread boundary.
    if (g_hup_requested) {
        if (g_hangupHook) {
            g_hangupHook();
        }
        g_hup_requested = 0;
    }

    // Safely execute the shutdown hook prior to returning the done state.
    if (g_done != 0) {
        if (g_shutdownHook) {
            g_shutdownHook();
            g_shutdownHook = nullptr; // Ensure it only runs once
        }
    }

    return g_done != 0;
}

int SignalManager::ExitIfDone(int status) {
    if (g_done) {
        std::exit(status);
    }
    return 1;
}

void SignalManager::TriggerShutdown() {
    g_done = 1;
}

}
