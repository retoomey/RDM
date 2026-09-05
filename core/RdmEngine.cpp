#include "RdmEngine.h"
#include "config.h"
#include "ProcessUtil.h"
#include "Registry.h"
#include "AutoShifter.h"
#include "StorageFactory.h"
#include "NetworkFactory.h"
#include "ConfParser.h"
#include "InfoFile.h"
#include "DownstreamClientHandler.h"
#include "UpstreamClientHandler.h"
#include "NetworkUtils.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <climits>

namespace rdm {

RdmEngine::RdmEngine() : Application("The Unidata Local Data Manager (LDM) server core engine.") {}

RdmEngine::~RdmEngine() {
    StopEngine();
}

int RdmEngine::StartEngine(int argc, char* argv[]) {
    return Execute(argc, argv);
}

void RdmEngine::StopEngine() {
    if (server_) {
        server_->Stop();
        server_.reset();
    }
    (void) uldb_.Remove(getpid());
    (void) uldb_.Close();

    const bool isTopProc = getpid() == getpgrp();
    if (isTopProc) {
        SignalManager::Ignore(SIGTERM);
        LogNotice("Terminating process group cleanly (children and grandchildren)");
        PrivilegeManager::Instance().RaisePrivileges();
        (void)kill(0, SIGTERM);
        PrivilegeManager::Instance().LowerPrivileges();
        while (Reap(-1, 0) > 0) {}
        (void) uldb_.Delete("");
    }
    (void)registry::close();
}

int RdmEngine::Daemonize() {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) exit(0);
    if (setsid() < 0) return 2;

    SignalManager::Ignore(SIGHUP);

    if ((pid = fork()) < 0) return 1;
    if (pid) {
        fmt::print(stdout, "{}\n", static_cast<long>(pid));
        exit(0);
    }

    for (int i = 0; i < 3; ++i) close(i);
    (void)open("/dev/null", O_RDONLY);
    (void)open("/dev/null", O_RDWR);
    (void)open("/dev/null", O_RDWR);
    return 0;
}

pid_t RdmEngine::Reap(pid_t pid, int options) {
    int status = 0;
    pid_t wpid = processManager_.Reap(pid, options, &status);
    if (wpid > 0 && WIFSIGNALED(status)) {
        switch (WTERMSIG(status)) {
            case SIGQUIT: case SIGILL: case SIGTRAP: case SIGABRT:
            case SIGFPE: case SIGBUS: case SIGSEGV: case SIGSYS:
            case SIGXCPU: case SIGXFSZ:
                LogNotice("Killing (SIGTERM) process group due to fatal child signal");
                (void) kill(0, SIGTERM);
                break;
        }
    }
    return wpid;
}

void RdmEngine::ConfigureOptions() {
    Application::ConfigureOptions();
    RegisterOption('I', "interface", "Use network interface address", "");
    RegisterOption('P', "port", "Port number (default 388)", "388");
    RegisterOption('M', "maxclients", "Max client limit", "256");
    RegisterOption('q', "queue", "Product-queue pathname", "");
    RegisterOption('o', "offset", "Data arrival time offset", "");
    RegisterOption('m', "maxlatency", "Max acceptable product latency", "3600");
    RegisterOption('t', "timeout", "Set RPC timeout", "60");
    RegisterFlag('n', "Check configuration file syntax only");
    RegisterFlag('N', "Disable Nagle's Algorithm (TCP_NODELAY)");
    RegisterFlag('D', "Force background daemonization mode");
    RegisterOption('H', "maxhereis", "Max size for HEREIS transfer", "16384");
}

bool RdmEngine::ProcessOptions() {
    if (!Application::ProcessOptions()) return false;

    if (IsSet('n')) {
        std::string configPath = positionalArgs_.empty() ? "ldmd.conf" : positionalArgs_[0];
        unsigned int checkPort = IsSet('P') ? std::stoul(GetOption('P')) : 388;
        ServerConfig config;
        bool success = ConfParser::Parse(configPath, config, checkPort, true);
        
        if (!success) {
            LogWarning("Configuration-file \"{}\" parsed with errors.", configPath);
            exit(EXIT_FAILURE);
        } else if (config.allowRules.empty() && config.acceptRules.empty() &&
                   config.execRules.empty() && config.requestRules.empty()) {
            LogNotice("Configuration-file \"{}\" has no entries.", configPath);
        } else {
            LogNotice("Syntax check successful. Exiting.");
        }
        exit(EXIT_SUCCESS);
    }

    ldmBindAddr_ = GetOption('I');
    if (ldmBindAddr_.empty()) {
        ldmBindAddr_ = registry::getString(registry::RegistryKey::Hostname);
    } else {
        registry::putString(registry::RegistryKey::Hostname, ldmBindAddr_);
    }

    unsigned int regPort = registry::getUint(registry::RegistryKey::Port);
    if (IsSet('P')) {
        ldmPort_ = std::stoul(GetOption('P'));
    } else if (regPort > 0) {
        ldmPort_ = regPort;
    }
    registry::putUint(registry::RegistryKey::Port, ldmPort_);

    if (IsSet('M')) {
        maxClients_ = std::stoul(GetOption('M'));
    }
    if (IsSet('m')) {
        registry::putUint(registry::RegistryKey::MaxLatency, std::stoul(GetOption('m')));
    }
    if (IsSet('t')) {
        registry::putUint(registry::RegistryKey::RpcTimeout, std::stoul(GetOption('t')));
    }
    if (IsSet('o')) {
        registry::putInt(registry::RegistryKey::TimeOffset, std::stoi(GetOption('o')));
    }
    if (IsSet('q')) {
        registry::setQueuePath(GetOption('q'));
    }
    if (!positionalArgs_.empty()) {
        registry::setLdmdConfigPath(positionalArgs_[0]);
    }
    if (IsSet('N')) disableNagles_ = true;
    if (IsSet('D')) becomeDaemon_ = true;
    if (IsSet('H')) maxHereis_ = std::stoul(GetOption('H'));

    auto maxLatency = registry::getUint(registry::RegistryKey::MaxLatency);
    auto effectiveOffset = registry::getTimeOffset();
    if (effectiveOffset > maxLatency) {
        LogError("invalid toffset ({}) > max_latency ({})", effectiveOffset, maxLatency);
        return false;
    }
    return true;
}

bool RdmEngine::Initialize() {
    if (!Application::Initialize()) return false;

    if (becomeDaemon_) {
        if (!registry::close()) return false;
        if (Daemonize()) return false;
    }
    if (getpgid(0) != getpid()) (void)setpgid(0, 0);

    SignalManager::SetShutdownHook([this]() {
        if (server_) server_->Stop();
    });

    return true;
}

int RdmEngine::Run() {
    int exitStatus = EXIT_SUCCESS;
    std::string configPath = registry::getLdmdConfigPath();
    ServerConfig config;

    if (!ConfParser::Parse(configPath, config, ldmPort_)) {
        LogFatal("Failed to parse configuration file: {}", configPath);
        return EXIT_FAILURE;
    }

    if (config.allowRules.empty() && config.acceptRules.empty() &&
        config.execRules.empty() && config.requestRules.empty()) {
        LogFatal("The configuration file \"{}\" is empty", configPath);
        return EXIT_FAILURE;
    }

    auto uldb_status = static_cast<int>(uldb_.Delete(""));
    if (uldb_status && static_cast<int>(UldbStatus::EXIST) != uldb_status) return EXIT_FAILURE;
    if (uldb_.Create("", maxClients_ * 1024) != UldbStatus::SUCCESS) return EXIT_FAILURE;

    aclManager_ = std::make_unique<AclManager>(std::move(config.allowRules), std::move(config.acceptRules));

    bool spawnSuccess = true;

    // Spawn EXEC actions (like pqact)
    for (const auto& execRule : config.execRules) {
        if (processManager_.SpawnExec(execRule) < 0) {
            spawnSuccess = false;
            break;
        }
    }

    // Spawn upstream requesters
    auto feedCount = config.requestRules.size();
    bool nagles = disableNagles_;
    unsigned int hereis = maxHereis_;

    // Spawn upstream requesters using the new dedicated binary
    if (spawnSuccess) {
        for (const auto& reqRule : config.requestRules) {
            std::string cmd = "rdm-request";
            cmd += " -h " + reqRule.upstreamHost;
            cmd += " -P " + std::to_string(reqRule.port);
            cmd += " -f " + reqRule.feedtype.ToString();
            cmd += " -p \"" + reqRule.pattern + "\"";
            cmd += " -c " + std::to_string(feedCount);
            if (nagles) cmd += " -N";
            cmd += " -H " + std::to_string(hereis);

            std::string qPath = registry::getQueuePath();
            if (!qPath.empty()) {
                cmd += " -q " + qPath;
            }

            if (log_is_enabled_debug) cmd += " -x";
            else if (log_is_enabled_info) cmd += " -v";
            
            std::string logDest = GetOption('l');
            if (!logDest.empty()) {
                cmd += " -l " + logDest;
            }

            ExecRule rule;
            rule.command = Wordexp(cmd);
            pid_t pid = processManager_.SpawnExec(rule);
            if (pid < 0) {
                LogFatal("Failed to spawn rdm-request for {}", reqRule.upstreamHost);
                spawnSuccess = false;
                break;
            }
        }
    }

    const std::string listener = "rdm-listen";
    if (spawnSuccess && aclManager_->RequiresServer()) {
        ExecRule serverRule;
        std::string cmd = listener;
        if (!ldmBindAddr_.empty()) {
            cmd += " -I " + ldmBindAddr_;
        }
        cmd += " -P " + std::to_string(ldmPort_);
        cmd += " -M " + std::to_string(maxClients_);
        
        if (log_is_enabled_debug) cmd += " -x";
        else if (log_is_enabled_info) cmd += " -v";
        
        std::string logDest = GetOption('l');
        if (!logDest.empty()) {
            cmd += " -l " + logDest;
        }

        std::string qPath = registry::getQueuePath();
        if (!qPath.empty()) {
            cmd += " -q " + qPath;
        }

        cmd += " " + configPath;
        
        serverRule.command = Wordexp(cmd);
        pid_t srvPid = processManager_.SpawnExec(serverRule);
        if (srvPid < 0) {
            LogFatal("Failed to spawn {}!", listener);
            spawnSuccess = false;
        }
    }

    if (!spawnSuccess) {
        LogError("Startup aborted due to spawn failure. Initiating teardown.");
        exitStatus = EXIT_FAILURE;
    } else {
        while (!SignalManager::IsDone() && processManager_.Count() > 0) {
            while (Reap(-1, WNOHANG) > 0) {}
            SignalManager::SleepResponsive(registry::getSystemInterval());
        }
    }

    LogNotice("RdmEngine shutting down: signaling children and waiting for termination...");
    SignalManager::Ignore(SIGTERM);
    processManager_.KillAll(SIGTERM);

    while (processManager_.Count() > 0) {
        Reap(-1, 0);
    }

    LogNotice("RdmEngine tracked child shutdown complete.");

    return exitStatus;
}

}
