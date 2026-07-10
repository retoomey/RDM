#include "NetworkApp.h"
#include "FeedType.h"
#include "Pattern.h"
#include "Log.h"
#include "IServiceHandler.h"
#include "Timestamp.h"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <climits>

using namespace rdm;

// =======================================================================
// Service Handler
// =======================================================================
class FeedmeHandler : public IServiceHandler {
private:
    ProdClass& clss_;
    unsigned int remaining_{0};
    Signature signature_{};
    int64_t arrival_sec_{0};
    int32_t arrival_usec_{0};
    ProdInfo last_info_;

    void PrintInfo(const ProdInfo& info) {
        if (!log_is_enabled_info) return;
        LogInfo("{}", info.ToString(log_is_enabled_debug));
    }

    void UpdateClassTime(int64_t sec, int32_t usec) {
        Timestamp t(sec, usec);
        t.IncrementMicrosecond();
        clss_.from_sec = t.tv_sec;
        clss_.from_usec = t.tv_usec;
    }

public:
    explicit FeedmeHandler(ProdClass& clss) : clss_(clss) {}

    bool IsConnectionAllowed(const std::string& hostname, 
       const std::string& ip) override { return true; }

    int OnHereIs(const PeerContext& peer, const Product& product) override {
        PrintInfo(product.info);
        
        // FIX: Use product.data and product.info.sz
        if (write(STDOUT_FILENO, product.data, product.info.sz) != static_cast<ssize_t>(product.info.sz)) {
            LogSyserr("data write failed");
            SignalManager::TriggerShutdown();
            return -1; 
        }
        
        // FIX: Route through the arrival wrapper
        UpdateClassTime(product.info.arrival.tv_sec, product.info.arrival.tv_usec);
        return 0;
    }

    int OnComingSoon(const PeerContext& peer, const ProdInfo& info, unsigned int pktsz) override {
        if (log_is_enabled_debug) {
            // FIX: Print the object directly!
            LogDebug("comingsoon: {} (pktsz {})", info.ToString(true), pktsz);
        }
        
        // FIX: Assign native object fields directly
        remaining_ = info.sz;
        signature_ = info.signature;
        arrival_sec_ = info.arrival.tv_sec;
        arrival_usec_ = info.arrival.tv_usec;
        last_info_ = info;
        
        return 0;
    }

    int OnBlkData(const PeerContext& peer, const uint8_t* signature, unsigned int pktnum, const uint8_t* data, unsigned int size) override {
        if (!signature_.Equals(signature)) {
            LogError("signature mismatch");
            return EINVAL;
        }
        remaining_ -= size;
        if (remaining_ == 0) {
            PrintInfo(last_info_);
            UpdateClassTime(arrival_sec_, arrival_usec_);
        }
        if (write(STDOUT_FILENO, data, size) != static_cast<ssize_t>(size)) {
            LogSyserr("data write failed");
            // Deprecated: exit(1);
            // Reason: Graceful signal shutdown preserves RAII lifecycle
            SignalManager::TriggerShutdown();
            return -1;
        }
        return 0;
    }

    int OnNotification(const PeerContext& peer, const ProdInfo& info) override { return 0; }
    HiyaResponse OnHiyaRequest(const PeerContext& peer, const HiyaRequest&) override 
      { return {ReplyStatus::DONT_SEND, 0, {}}; }
    FeedResponse OnFeedRequest(const PeerContext& peer, const FeedRequest&) override 
      { return {ReplyStatus::DONT_SEND, 0, {}}; }
    int StreamProducts(std::shared_ptr<IClient>) override { return -1; }
    bool IsAlive(unsigned int) override { return true; }
    pid_t ReapChildProcess() override { return 0; }
};

// =======================================================================
// Application Core
// =======================================================================
class FeedmeApp : public NetworkApp {
private:
    ProdClass clss_;
    unsigned totalTimeo_{300}; // The original feedme.cpp used a 300 second total timeout by default.

protected:
    void ConfigureOptions() override {
        NetworkApp::ConfigureOptions();
        
        RegisterOption('f', "feedtype", "Interested in products from feed 'feedtype'", "ANY");
        RegisterOption('p', "pattern", "Interested in products matching 'pattern'", ".*");
        RegisterOption('o', "offset", "Set the 'from' time offset secs before now", "0");
        RegisterOption('T', "TotalTimeo", "Give up after this many secs", "300");
    }

    bool ProcessOptions() override {
        if (!NetworkApp::ProcessOptions()) return false;

        Timestamp now = Timestamp::Now();
        clss_.from_sec = now.tv_sec;
        clss_.from_usec = now.tv_usec;
        clss_.to_sec = 0x7fffffff;
        clss_.to_usec = 999999;

        ProdSpec spec;
        spec.feedtype = 0xffffffff;
        spec.pattern = ".*";

        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), spec.feedtype) != FEEDTYPE_OK) {
                LogError("Bad feedtype \"{}\"", GetOption('f'));
                return false;
            }
        }
        if (IsSet('p')) spec.pattern = GetOption('p');
        if (IsSet('o')) clss_.from_sec -= std::stoul(GetOption('o'));
        if (IsSet('T')) totalTimeo_ = std::stoul(GetOption('T'));

        clss_.specs.push_back(spec);
        return true;
    }

    bool Initialize() override {
        if (!NetworkApp::Initialize()) return false;

        LogNotice("Starting Up: {}", remoteHost_);

        if (totalTimeo_ > 0) {
            struct sigaction sa;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sa.sa_handler = [](int) { SignalManager::TriggerShutdown(); };
            sigaction(SIGALRM, &sa, nullptr);
            alarm(totalTimeo_);
        }
        return true;
    }

    int Run() override {
        auto handler = std::make_shared<FeedmeHandler>(clss_);
        auto client = CreateClient();
        if (!client) return EXIT_FAILURE;

        while(!SignalManager::IsDone()){
            if (client->Connect() != 0) {
                LogError("Connection failed: {}", client->GetLastError());

                if (SignalManager::IsDone()) break;

                sleep(10);
                continue;
            }

            FeedRequest req;
            req.isNotifier = false;
            req.maxHereis = UINT_MAX;
            req.requestedClass = clss_;

            FeedResponse resp = client->SubscribeAndListen(req, handler, timeo_);

            if (resp.statusCode != ReplyStatus::OK) {
                LogError("FEEDME failed: {}", client->GetLastError());
            }

            client->Disconnect();
            sleep(10);
        }

        return EXIT_SUCCESS;
    }

public:
    FeedmeApp() : NetworkApp("Connects to an upstream LDM daemon and requests a data feed.") {}
};

int main(int argc, char *argv[]) {
    FeedmeApp app;
    return app.Execute(argc, argv);
}
