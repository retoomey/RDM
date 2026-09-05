#include "config.h"
#include "Application.h"
#include "Log.h"
#include "Registry.h"
#include "AutoShifter.h"
#include "StorageFactory.h"
#include "NetworkFactory.h"
#include "InfoFile.h"
#include "DownstreamClientHandler.h"
#include "NetworkUtils.h"
#include "Timestamp.h"
#include "SignalManager.h"
#include "ServiceAddr.h"
#include <memory>
#include <string>
#include <unistd.h>
#include <climits>

using namespace rdm;

class RdmRequestApp : public Application {
private:
    std::string upstreamHost_;
    unsigned int port_{388};
    FeedType feedtype_{ANY};
    std::string pattern_{".*"};
    bool isPrimary_{true};
    unsigned int feedCount_{1};
    bool disableNagles_{false};
    unsigned int maxHereis_{16384};

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

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterOption('q', "queue", "Product-queue pathname", "");
        RegisterOption('h', "host", "Upstream host to request data from", "");
        RegisterOption('P', "port", "Upstream port", "388");
        RegisterOption('f', "feedtype", "Requested feedtype", "ANY");
        RegisterOption('p', "pattern", "Requested pattern", ".*");
        RegisterFlag('1', "Primary connection flag (default is true unless disabled)");
        RegisterOption('c', "count", "Feed count for AutoShifter", "1");
        RegisterFlag('N', "Disable Nagle's Algorithm (TCP_NODELAY)");
        RegisterOption('H', "maxhereis", "Max size for HEREIS transfer", "16384");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;

        if (IsSet('q')) registry::setQueuePath(GetOption('q'));
        if (IsSet('h')) upstreamHost_ = GetOption('h');
        if (upstreamHost_.empty()) {
            LogError("Upstream host (-h) is required for rdm-request");
            return false;
        }

        if (IsSet('P')) port_ = std::stoul(GetOption('P'));
        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), feedtype_) != FEEDTYPE_OK) {
                LogError("Invalid feedtype: {}", GetOption('f'));
                return false;
            }
        }
        if (IsSet('p')) pattern_ = GetOption('p');
        
        isPrimary_ = IsSet('1');
        if (IsSet('c')) feedCount_ = std::stoul(GetOption('c'));
        if (IsSet('N')) disableNagles_ = true;
        if (IsSet('H')) maxHereis_ = std::stoul(GetOption('H'));

        return true;
    }

    int Run() override {
        int errCode = 0;
        int maxSilence = 10 * registry::getSystemInterval();
        unsigned int backoffTime = registry::getTimeOffset();
        
        ProdClass clssp;
        clssp.from_sec = 0; 
        clssp.from_usec = 0;
        clssp.to_sec = 0x7fffffff; 
        clssp.to_usec = 999999;
        clssp.specs.push_back({feedtype_, pattern_});

        Timestamp defaultFrom = Timestamp::Now();
        defaultFrom.tv_sec -= backoffTime;
        if (defaultFrom.tv_sec > clssp.from_sec ||
           (defaultFrom.tv_sec == clssp.from_sec && defaultFrom.tv_usec > clssp.from_usec)) {
            clssp.from_sec = defaultFrom.tv_sec;
            clssp.from_usec = defaultFrom.tv_usec;
        }

        auto shifter = std::make_shared<AutoShifter>(isPrimary_, feedCount_,
          static_cast<double>(registry::getSystemInterval()));

        SavedInfoFile stateManager(getLDMDInfoPrefix(), upstreamHost_, port_, clssp);
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

        ServiceAddr target(upstreamHost_, port_);
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
                if (disableNagles_) client->DisableNagles();
                FeedRequest req;
                req.isNotifier = false;
                req.maxHereis = isPrimary_ ? UINT_MAX : 0;
                req.requestedClass = modernClass;
                
                FeedResponse resp = client->SubscribeAndListen(req, handler, maxSilence);
                if (resp.statusCode != ReplyStatus::OK) {
                    LogError("Upstream feed request failed: {}", client->GetLastError());
                }
                
                client->Disconnect();
                if (shifter->ShouldSwitch()) {
                    isPrimary_ = !isPrimary_;
                    shifter->Init(isPrimary_);
                    doSleep = 0;
                }
            } else {
                LogError("Couldn't connect to upstream {}:{} - {}", upstreamHost_, port_, client->GetLastError());
                if (shifter->ShouldSwitch()) {
                    isPrimary_ = !isPrimary_;
                    shifter->Init(isPrimary_);
                    doSleep = 0;
                }
            }

            if (!errCode && doSleep) {
                if (!errCode && doSleep) {
                  SignalManager::SleepResponsive(2 * registry::getSystemInterval());
                } 
            }
        }

        return errCode;
    }

public:
    RdmRequestApp() : Application("The outbound fetcher (satisfies REQUEST rules).") {}
};

int main(int argc, char* argv[]) {
    RdmRequestApp app;
    return app.Execute(argc, argv);
}
