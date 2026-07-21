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

// Retain all internal helper functions from the original ldmd.cpp here
static std::string getLDMDInfoPrefix() {
    static std::string infoDir;
    if (infoDir.empty()) {
       infoDir = registry::getString(registry::RegistryKey::StatePath);
    }
    return infoDir;
}

static int getQueueProdInfo(IProductStore& pq_local, const ProdClass& prodClass, ProdInfo& info) {
    int status = -1;
    auto cursor = pq_local.CreateCursor();
    cursor->setCursor(Timestamp::ENDT);
    info.arrival.tv_sec = 0;
    while ((status = cursor->next(true, prodClass,
        [](const prod_par_t* prod_par, const queue_par_t*, void* arg) {
            ProdInfo* outInfo = static_cast<ProdInfo*>(arg);
            *outInfo = prod_par->info;
        }, false, &info)) == 0) {
        if (SignalManager::IsDone()) break;
        if (info.arrival.tv_sec != 0) return 0;
        Timestamp clean_cursor;
        cursor->getCursorTimestamp(clean_cursor);
        if (prodClass.from_sec > 0 && (clean_cursor.tv_sec < prodClass.from_sec)) break;
    }
    if (status && static_cast<int>(PqStatus::End) != status) {
        LogError("getQueueProdInfo(): failure (status = {})", status);
    } else {
        status = (0 == status || info.arrival.tv_sec == 0) ? 1 : 0;
    }
    return status;
}

static int requester_core(const char* source, const unsigned port, ProdClass clssp,
                          int isPrimary, const unsigned feedCount,
                          bool disableNagles, unsigned int maxHereis) {
    int errCode = 0;
    int maxSilence = 10 * registry::getSystemInterval();
    unsigned int backoffTime = registry::getTimeOffset();
    Timestamp defaultFrom = Timestamp::Now();
    defaultFrom.tv_sec -= backoffTime;
    if (defaultFrom.tv_sec > clssp.from_sec ||
       (defaultFrom.tv_sec == clssp.from_sec && defaultFrom.tv_usec > clssp.from_usec)) {
        clssp.from_sec = defaultFrom.tv_sec;
        clssp.from_usec = defaultFrom.tv_usec;
    }
    auto shifter = std::make_shared<AutoShifter>(isPrimary == 1, feedCount,
      static_cast<double>(registry::getSystemInterval()));
    SavedInfoFile stateManager(getLDMDInfoPrefix(), source, port, clssp);
    ProdInfo latestInfo;
    latestInfo.arrival.tv_sec = 0;
    bool hasLatest = stateManager.Read(latestInfo);
    auto serializer = NetworkFactory::CreateSerializer();
    if (!hasLatest) {
        std::unique_ptr<IProductStore> localQueue = StorageFactory::Create(serializer);
        if (localQueue->open(registry::getQueuePath().c_str(), PqFlags::ReadOnly) == 0) {
            if (getQueueProdInfo(*localQueue, clssp, latestInfo) == 0) {
                hasLatest = true;
            }
        }
    }
    struct StateFlusher {
        SavedInfoFile& sm;
        ProdInfo& info;
        bool& hasInfo;
        ~StateFlusher() { if (hasInfo) sm.Write(info); }
    } flusher{stateManager, latestInfo, hasLatest};
    std::unique_ptr<IProductStore> persistentQueue = StorageFactory::Create(serializer);
    errCode = persistentQueue->open(registry::getQueuePath().c_str(), PqFlags::Default);
    if (errCode) return errCode;
    auto handler = std::make_shared<DownstreamClientHandler>(
        persistentQueue.get(),
        [&latestInfo, &hasLatest, shifter](const ProdInfo& info, int success) {
            if (success) {
                latestInfo = info;
                hasLatest = true;
            }
            shifter->Process(success, info.sz);
        }
    );
    ServiceAddr target(source, port);
    unsigned rpcTimeout = registry::getUint(registry::RegistryKey::RpcTimeout);
    auto client = NetworkFactory::CreateClient(std::move(target), rpcTimeout);
    while (!errCode && !SignalManager::IsDone()) {
        int doSleep = 1;
        defaultFrom = Timestamp::Now();
        defaultFrom.tv_sec -= backoffTime;
        if (defaultFrom.tv_sec > clssp.from_sec ||
           (defaultFrom.tv_sec == clssp.from_sec && defaultFrom.tv_usec > clssp.from_usec)) {
            clssp.from_sec = defaultFrom.tv_sec;
            clssp.from_usec = defaultFrom.tv_usec;
        }
        auto modernClass = clssp;
        if (hasLatest && latestInfo.arrival.tv_sec != -1) {
            ProdSpec sigSpec;
            sigSpec.feedtype = NONE;
            sigSpec.pattern = "SIG=" + latestInfo.signature.ToString();
            modernClass.specs.push_back(sigSpec);
            LogNotice("Resuming upstream feed from state file: {}", sigSpec.pattern);
        }
        if (client->Connect() == 0) {
            if (disableNagles) client->DisableNagles();
            FeedRequest req;
            req.isNotifier = false;
            req.maxHereis = isPrimary? UINT_MAX : 0;
            req.requestedClass = modernClass;
            FeedResponse resp = client->SubscribeAndListen(req, handler, maxSilence);
            if (resp.statusCode != ReplyStatus::OK) {
                LogError("Upstream feed request failed: {}", client->GetLastError());
            }
            client->Disconnect();
            if (shifter->ShouldSwitch()) {
                isPrimary = !isPrimary;
                shifter->Init(isPrimary == 1);
                doSleep = 0;
            }
        } else {
            LogError("Couldn't connect to upstream LDM {}:{} - {}", source, port, client->GetLastError());
            if (shifter->ShouldSwitch()) {
                isPrimary = !isPrimary;
                shifter->Init(isPrimary == 1);
                doSleep = 0;
            }
        }
        if (!errCode && doSleep) {
            if (SignalManager::IsDone()) break;
            sleep(2 * registry::getSystemInterval());
        }
    }
    return errCode;
}

static void requester_exec(const char* source, const unsigned port, ProdClass clssp,
                           int isPrimary, const unsigned feedCount,
                           bool disableNagles, unsigned int maxHereis) {
    int errCode = requester_core(source, port, clssp, isPrimary, feedCount, disableNagles, maxHereis);
    exit(errCode);
}

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
        // Protect the parent so it can finish reaping
        SignalManager::Ignore(SIGTERM);
        LogNotice("Terminating process group cleanly (children and grandchildren)");
        
        // Broadcast SIGTERM to the isolated group. 
        // Because of setpgid(0,0), this safely ignores the RAPIO supervisor.
        PrivilegeManager::Instance().RaisePrivileges();
        (void)kill(0, SIGTERM);
        PrivilegeManager::Instance().LowerPrivileges();
        
        // Wait for the tree to collapse
        while (Reap(-1, 0) > 0) {}
        if (!checkOnly_) (void) uldb_.Delete("");
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
    RegisterFlag('D', "Force background daemonization mode"); // Fix implemented here
    RegisterOption('H', "maxhereis", "Max size for HEREIS transfer", "16384");
}

bool RdmEngine::ProcessOptions() {
    if (!Application::ProcessOptions()) return false;
    ldmBindAddr_ = GetOption('I');
    if (IsSet('P')) ldmPort_ = std::stoul(GetOption('P'));
    if (IsSet('M')) maxClients_ = std::stoul(GetOption('M'));
    if (IsSet('m')) registry::putUint(registry::RegistryKey::MaxLatency, std::stoul(GetOption('m')));
    if (IsSet('t')) registry::putUint(registry::RegistryKey::RpcTimeout, std::stoul(GetOption('t')));
    if (IsSet('o')) registry::putInt(registry::RegistryKey::TimeOffset, std::stoi(GetOption('o')));
    if (IsSet('N')) disableNagles_ = true;
    if (IsSet('D')) becomeDaemon_ = true; // Fix implemented here
    if (IsSet('H')) maxHereis_ = std::stoul(GetOption('H'));
    checkOnly_ = IsSet('n');
    
    auto maxLatency = registry::getUint(registry::RegistryKey::MaxLatency);
    auto effectiveOffset = registry::getTimeOffset();
    if (effectiveOffset > maxLatency) {
        LogError("invalid toffset ({}) > max_latency ({})", effectiveOffset, maxLatency);
        return false;
    }
    if (IsSet('q')) registry::setQueuePath(GetOption('q'));
    if (!positionalArgs_.empty()) {
        registry::setLdmdConfigPath(positionalArgs_[0]);
    }
    return true;
}

bool RdmEngine::Initialize() {
    if (!Application::Initialize()) return false;

    // Compile-time DONTFORK macro removed. 
    // Purely programmatic runtime control now.
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
    std::string configPath = registry::getLdmdConfigPath();
    ServerConfig config = ConfParser::Parse(configPath, ldmPort_);
    
    if (config.allowRules.empty() && config.acceptRules.empty() &&
        config.execRules.empty() && config.requestRules.empty()) {
        LogFatal("The configuration file \"{}\" is empty", configPath);
        return EXIT_FAILURE;
    }
    
    if (!checkOnly_) {
        auto uldb_status = static_cast<int>(uldb_.Delete(""));
        if (uldb_status && static_cast<int>(UldbStatus::EXIST) != uldb_status) return EXIT_FAILURE;
        if (uldb_.Create("", maxClients_ * 1024) != UldbStatus::SUCCESS) return EXIT_FAILURE;
        
        aclManager_ = std::make_unique<AclManager>(std::move(config.allowRules), std::move(config.acceptRules));
        
        // Spawn EXEC actions (like pqact)
        for (const auto& execRule : config.execRules) {
            if (processManager_.SpawnExec(execRule) < 0) return EXIT_FAILURE;
        }
        
        auto feedCount = config.requestRules.size();
        bool nagles = disableNagles_;
        unsigned int hereis = maxHereis_;
        
        // Spawn upstream requesters
        for (const auto& reqRule : config.requestRules) {
            pid_t pid = processManager_.SpawnRequester(reqRule.upstreamHost, [&reqRule, feedCount, nagles, hereis]() {
                ProdClass clss;
                clss.from_sec = 0; clss.from_usec = 0;
                clss.to_sec = 0x7fffffff; clss.to_usec = 999999;
                clss.specs.push_back({reqRule.feedtype, reqRule.pattern});
                requester_exec(reqRule.upstreamHost.c_str(), reqRule.port, clss, 1, feedCount, nagles, hereis);
            });
            if (pid < 0) return EXIT_FAILURE;
        }
        
        // NEW LOGIC: Spawn pqbroker if we need a listening port
        if (aclManager_->RequiresServer()) {
            ExecRule serverRule;
            std::string cmd = "pqbroker";
            
            cmd += " -P " + std::to_string(ldmPort_);
            if (disableNagles_) cmd += " -N";
            
            // Forward logging level
            if (log_is_enabled_debug) cmd += " -x";
            else if (log_is_enabled_info) cmd += " -v";
            
            // Forward log destination if set
            std::string logDest = GetOption('l');
            if (!logDest.empty()) {
                cmd += " -l " + logDest;
            }

            // Forward the specific queue path so ULDB shared memory syncs up!
            std::string qPath = registry::getQueuePath();
            if (!qPath.empty()) {
                cmd += " -q " + qPath;
            }

            // Pass the config file down
            cmd += " " + configPath;
            
            serverRule.command = Wordexp(cmd);
            pid_t srvPid = processManager_.SpawnExec(serverRule);
            if (srvPid < 0) {
                LogFatal("Failed to spawn pqbroker!");
                return EXIT_FAILURE;
            }
        }

        // ==========================================================
        // BULLETPROOF SUPERVISOR LOOP
        // ==========================================================
        // Unconditionally wait here as long as we have active children
        while (!SignalManager::IsDone() && processManager_.Count() > 0) {
            while (Reap(-1, WNOHANG) > 0) {}
            sleep(registry::getSystemInterval());
        }
    }
    
    return EXIT_SUCCESS;
}

} // namespace rdm
