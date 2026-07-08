/**
 * @file notifyme.cpp
 * @brief Modernized utility to request notification of available data-products from an LDM.
 * @author Robert Toomey
 * @date May 2026
 */
#include "config.h"
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
class NotifymeHandler : public IServiceHandler {
private:
    bool showProdOrigin_;
    ProdClass& clss_;

public:
    explicit NotifymeHandler(bool showOrigin, ProdClass& clss)
        : showProdOrigin_(showOrigin), clss_(clss) {}

    bool IsConnectionAllowed(const std::string& hostname, 
       const std::string& ip) override { return true; }

    int OnNotification(const PeerContext& peer, const ProdInfo& info) override {
        // FIX: Route through the arrival wrapper class
        Timestamp t(info.arrival.tv_sec, info.arrival.tv_usec);
        t.IncrementMicrosecond();
        clss_.from_sec = t.tv_sec;
        clss_.from_usec = t.tv_usec;

        if (log_is_enabled_info) {
            // FIX: Print the object directly! No more mapping needed.
            if (showProdOrigin_) {
                LogInfo("{} {}", info.ToString(log_is_enabled_debug), info.origin);
            } else {
                LogInfo("{}", info.ToString(log_is_enabled_debug));
            }
        }
        return 0;
    }

    int OnHereIs(const PeerContext& peer, const Product&) override { return 0; }
    int OnComingSoon(const PeerContext& peer, const ProdInfo&, unsigned int) override { return 0; }
    int OnBlkData(const PeerContext& peer, const uint8_t*, unsigned int, const uint8_t*, unsigned int) override { return 0; }
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
class NotifymeApp : public NetworkApp {
private:
    ProdClass clss_;
    unsigned totalTimeo_{300};
    bool showProdOrigin_{false};

protected:
    void ConfigureOptions() override {
        NetworkApp::ConfigureOptions();
        
        RegisterFlag('O', "Include product origin in verbose output");
        RegisterOption('f', "feedtype", "Interested in products from feed 'feedtype'", "ANY");
        RegisterOption('p', "pattern", "Interested in products matching 'pattern'", ".*");
        RegisterOption('o', "offset", "Set the 'from' time offset secs before now", "0");
        RegisterOption('T', "TotalTimeo", "Give up after this many secs", "300");
    }

    bool ProcessOptions() override {
        if (!NetworkApp::ProcessOptions()) return false;

        showProdOrigin_ = IsSet('O');

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

        // Configure a hard exit via SIGALRM if total timeout is reached
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
        auto handler = std::make_shared<NotifymeHandler>(showProdOrigin_, clss_);
        
        // CreateClient is automatically provided by NetworkApp 
        auto client = CreateClient();
        if (!client) return EXIT_FAILURE;

        while (!SignalManager::IsDone()){
            if (client->Connect() != 0) {
                LogError("Connection failed: {}", client->GetLastError());
                sleep(10);
                continue;
            }

            // The core difference from feedme is setting isNotifier to true 
            // and using the NOTIFYME RPC wrapper
            FeedRequest req;
            req.isNotifier = true; 
            req.maxHereis = 0; 
            req.requestedClass = clss_;

            FeedResponse resp = client->SubscribeAndListen(req, handler, timeo_);

            if (resp.statusCode != ReplyStatus::OK) {
                LogError("NOTIFYME failed: {}", client->GetLastError());
            }

            client->Disconnect();
            sleep(10);
        }

        return EXIT_SUCCESS;
    }

public:
    NotifymeApp() : NetworkApp("Connects to an upstream LDM daemon and requests product metadata notifications.") {}
};

int main(int argc, char *argv[]) {
    NotifymeApp app;
    return app.Execute(argc, argv);
}
