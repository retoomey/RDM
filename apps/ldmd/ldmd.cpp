/**
 * @file ldmd.cpp
 * @brief Modernized LDM server mainline program module
 * @author Robert Toomey
 * @date May 2026
 */
#include "config.h"
#include "Application.h"
#include "ProcessUtil.h"
#include "Registry.h"
#include "ULDB.h"
#include "AutoShifter.h"
#include "FeedType.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "NetworkUtils.h"
#include "IServer.h"
#include "ConfParser.h"
#include "AclManager.h"
#include "ProcessManager.h"
#include "InfoFile.h"
#include "UpstreamClientHandler.h"
#include "DownstreamClientHandler.h"
#include "NetworkUtils.h"
#include "NetworkFactory.h"
#include <iostream>
#include <string>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>
#include <csignal>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace rdm;

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
            outInfo->arrival.tv_sec = prod_par->info.arrival.tv_sec;
            outInfo->arrival.tv_usec = prod_par->info.arrival.tv_usec;
            outInfo->feedtype = prod_par->info.feedtype;
            outInfo->seqno = prod_par->info.seqno;
            outInfo->sz = prod_par->info.sz;
            outInfo->signature = prod_par->info.signature;
            outInfo->origin = prod_par->info.origin;
            outInfo->ident = prod_par->info.ident;
        }, false, &info)) == 0) {
        if (SignalManager::IsDone()) break;
        if (info.arrival.tv_sec != 0) return 0;
        Timestamp clean_cursor;
        cursor->getCursorTimestamp(clean_cursor);
        if (prodClass.from_sec > 0 && (clean_cursor.tv_sec < prodClass.from_sec)) {
            break;
        }
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
            if (SignalManager::IsDone()) break;
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

class LdmdApp : public Application {
private:
    std::string ldmBindAddr_;
    unsigned ldmPort_{388};
    unsigned maxClients_{256};
    bool becomeDaemon_{true};
    bool checkOnly_{false};
    bool disableNagles_{false};
    unsigned int maxHereis_{16384};
    std::unique_ptr<IServer> server_;
    Uldb uldb_; // [NEW] Explicit instance mapping replaces the global singleton state
    std::unique_ptr<AclManager> aclManager_;
    ProcessManager processManager_; 

    int Daemonize() {
        pid_t pid = fork();
        if (pid < 0) {
            LogSyserr("fork() failure");
            return 1;
        }
        if (pid > 0) exit(0);
        if (setsid() < 0) {
            LogSyserr("setsid() failure");
            return 2;
        }
        SignalManager::Ignore(SIGHUP);
        if ((pid = fork()) < 0) {
            LogSyserr("fork() failure");
            return 1;
        }
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

    void KillProcGroup() {
        PrivilegeManager::Instance().RaisePrivileges();
        (void)kill(0, SIGTERM);
        PrivilegeManager::Instance().LowerPrivileges();
    }

    pid_t Reap(pid_t pid, int options) {
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

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterOption('I', "interface", "Use network interface associated with given IP address", "");
        RegisterOption('P', "port", "The port number for LDM connections (default 388)", "388");
        RegisterOption('M', "maxclients", "Maximum number of clients (default 256)", "256");
        RegisterOption('q', "queue", "Product-queue pathname", "");
        RegisterOption('o', "offset", "Start with products arriving 'offset' seconds before now", "");
        RegisterOption('m', "maxlatency", "The maximum acceptable data-product latency in seconds", "3600");
        RegisterOption('t', "timeout", "Set RPC timeout to 'timeout' seconds", "60");
        RegisterFlag('n', "Do nothing other than check the configuration-file");
        RegisterFlag('N', "Disable Nagle's Algorithm (TCP_NODELAY)");
        RegisterOption('H', "maxhereis", "Max size for HEREIS product transfer", "16384");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;
        ldmBindAddr_ = GetOption('I');
        if (IsSet('P')) ldmPort_ = std::stoul(GetOption('P'));
        if (IsSet('M')) maxClients_ = std::stoul(GetOption('M'));
        if (IsSet('m')) registry::putUint(registry::RegistryKey::MaxLatency, std::stoul(GetOption('m')));
        if (IsSet('t')) registry::putUint(registry::RegistryKey::RpcTimeout, std::stoul(GetOption('t')));
        if (IsSet('o')) registry::putInt(registry::RegistryKey::TimeOffset, std::stoi(GetOption('o')));
        if (IsSet('N')) disableNagles_ = true;
        if (IsSet('H')) maxHereis_ = std::stoul(GetOption('H'));
        checkOnly_ = IsSet('n');
        if (IsSet('l') && GetOption('l') == "-") {
            becomeDaemon_ = false;
        }
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

    bool Initialize() override {
        if (!Application::Initialize()) return false;
#ifndef DONTFORK
        if (becomeDaemon_) {
            if (!registry::close()) {
                LogFatal("reg_close() failure");
                return false;
            }
            if (Daemonize()) {
                LogFatal("daemonize() failure");
                return false;
            }
        }
#endif
        if (getpgid(0) != getpid()) (void)setpgid(0, 0);
        SignalManager::SetShutdownHook([this]() {
            if (server_) server_->Stop();
        });
        return true;
    }

    int Run() override {
        LogDebug("Parsing configuration file");
        std::string configPath = registry::getLdmdConfigPath();
        ServerConfig config = ConfParser::Parse(configPath, ldmPort_);
        if (config.allowRules.empty() && config.acceptRules.empty() &&
            config.execRules.empty() && config.requestRules.empty()) {
            LogFatal("The LDM configuration-file \"{}\" is effectively empty", configPath);
            return EXIT_FAILURE;
        }
        LogNotice("Starting Up (version: {}; built: {} {})", PACKAGE_VERSION, __DATE__, __TIME__);
        if (!checkOnly_) {
            LogDebug("Creating shared upstream LDM database");
            
            auto uldb_status = static_cast<int>(uldb_.Delete(""));
            if (uldb_status && static_cast<int>(UldbStatus::EXIST) != uldb_status) {
               LogFatal("Couldn't delete existing shared upstream LDM database");
               return EXIT_FAILURE;
            }
            if (uldb_.Create("", maxClients_ * 1024) != UldbStatus::SUCCESS) {
                LogFatal("Couldn't create shared upstream LDM database");
                return EXIT_FAILURE;
            }
            aclManager_ = std::make_unique<AclManager>(std::move(config.allowRules), 
               std::move(config.acceptRules));
            for (const auto& execRule : config.execRules) {
                if (processManager_.SpawnExec(execRule) < 0) {
                    LogFatal("Couldn't start EXEC entry");
                    return EXIT_FAILURE;
                }
            }
            auto feedCount = config.requestRules.size();
            bool nagles = disableNagles_;
            unsigned int hereis = maxHereis_;
            for (const auto& reqRule : config.requestRules) {
                pid_t pid = processManager_.SpawnRequester(reqRule.upstreamHost,
                  [&reqRule, feedCount, nagles, hereis]() {
                    ProdClass clss;
                    clss.from_sec = 0;
                    clss.from_usec = 0;
                    clss.to_sec = 0x7fffffff;
                    clss.to_usec = 999999;
                    ProdSpec spec;
                    spec.feedtype = reqRule.feedtype;
                    spec.pattern = reqRule.pattern;
                    clss.specs.push_back(spec);
                    requester_exec(reqRule.upstreamHost.c_str(), reqRule.port, clss, 1, feedCount, nagles, hereis);
                });
                if (pid < 0) {
                    LogFatal("Couldn't start REQUEST entry");
                    return EXIT_FAILURE;
                }
            }
            if (aclManager_->RequiresServer()) {
                LogDebug("Creating service portal via Anti-Corruption Layer");
                server_ = NetworkFactory::CreateServer();
                LogDebug("Serving socket");
                
                auto handler = std::make_shared<UpstreamServerHandler>(
                    *aclManager_,
                    uldb_,
                    processManager_
                );
                if (server_->Start(ldmBindAddr_, ldmPort_, maxClients_, handler, processManager_) != 0) {
                    LogFatal("Failed to start RPC service portal");
                    return EXIT_FAILURE;
                }
            } else {
                while (!SignalManager::IsDone() && processManager_.Count() > 0) {
                    while (Reap(-1, WNOHANG) > 0) {}
                    sleep(registry::getSystemInterval());
                }
            }
        }
        return EXIT_SUCCESS;
    }

public:
    LdmdApp() : Application("The Unidata Local Data Manager (LDM) server daemon.") {}
    ~LdmdApp() override {
        if (server_) {
            server_->Stop();
            server_.reset();
        }
        (void) uldb_.Remove(getpid());
        (void) uldb_.Close();
        const bool isTopProc = getpid() == getpgrp();
        if (isTopProc) {
            SignalManager::Ignore(SIGTERM);
            LogNotice("Terminating process group");
            KillProcGroup();
            while (Reap(-1, 0) > 0) {}
            if (!checkOnly_) (void) uldb_.Delete("");
        }
        if (!registry::close()){
            LogError("Failed to close registry cleanly");
        }
    }
};

int main(int argc, char* argv[]) {
    LdmdApp app;
    return app.Execute(argc, argv);
}
