#include "config.h"
#include "QueueApp.h"
#include "Log.h"
#include "FeedType.h"
#include "Pattern.h"
#include "ServiceAddr.h"
#include "NetworkFactory.h"
#include "IClient.h"
#include "Timestamp.h"
#include "SignalManager.h"
#include <iostream>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <climits>

using namespace rdm;

struct SendStats {
    int nprods{0};
    int nconnects{0};
    int ndisco{0};
    double min_latency{2147483647.0};
    double max_latency{0.0};
};

class PqSendApp : public QueueApp {
private:
    std::string remoteHost_{"localhost"};
    unsigned int port_{388};
    unsigned int interval_{15};
    unsigned int rpcTimeout_{25};
    unsigned int totalTimeo_{3600};
    
    ProdClass offerClass_;
    ProdClass wantClass_;
    SendStats stats_;

    // Static callback wrapper for QueueCursor
    static int send_callback(const ProdInfo& info, const void* datap, void* xprod, size_t size, void* arg) {
        auto* app = static_cast<PqSendApp*>(arg);
        return app->ProcessProduct(info, datap);
    }

    int ProcessProduct(const ProdInfo& info, const void* datap) {
        // Ensure the product matches what the server explicitly accepted via HIYA RECLASS
        if (!wantClass_.Contains(info)) {
            LogDebug("{} doesn't want {}", remoteHost_, info.ToString(false));
            return 0;
        }

        Product prod;
        prod.info = info;
        prod.data = static_cast<const uint8_t*>(datap);

        // The IClient handles the branching between HEREIS and COMINGSOON internally
        int status = client_->SendProduct(prod);

        if (status == 0) {
            Timestamp now = Timestamp::Now();
            double latency = (now - info.arrival).AsSeconds();
            stats_.nprods++;
            if (latency < stats_.min_latency) stats_.min_latency = latency;
            if (latency > stats_.max_latency) stats_.max_latency = latency;

            if (log_is_enabled_info) {
                LogInfo("Sent: {}", info.ToString(log_is_enabled_debug));
            }
        } else {
            LogError("Failed to send product: {}", info.ident);
            return status; // Bubble error up to halt cursor sequencing
        }
        return 0;
    }

    std::shared_ptr<IClient> client_;

protected:
    void ConfigureOptions() override {
        QueueApp::ConfigureOptions();
        RegisterOption('h', "remote", "Send to the LDM on 'remote' (default: localhost)", "localhost");
        RegisterOption('P', "port", "Set the port number (default: 388)", "388");
        RegisterOption('f', "feedtype", "Send products matching 'feedtype'", "ANY");
        RegisterOption('p', "pattern", "Send products matching 'pattern'", ".*");
        RegisterOption('i', "interval", "Poll queue every 'interval' seconds (0 = run once)", "15");
        RegisterOption('t', "timeout", "RPC timeout in seconds", "25");
        RegisterOption('T', "TotalTimeo", "Terminate after this many seconds", "3600");
        RegisterOption('o', "offset", "Send products inserted no earlier than 'offset' seconds ago", "");
        RegisterFlag('d', "Decouple TotalTimeo (-T) and offset (-o)");
    }

    bool ProcessOptions() override {
        if (!QueueApp::ProcessOptions()) return false;
        
        if (IsSet('h')) remoteHost_ = GetOption('h');
        if (IsSet('P')) port_ = std::stoul(GetOption('P'));
        if (IsSet('i')) interval_ = std::stoul(GetOption('i'));
        if (IsSet('t')) rpcTimeout_ = std::stoul(GetOption('t'));
        if (IsSet('T')) totalTimeo_ = std::stoul(GetOption('T'));
        
        // Define coupledTimes as the inverse of the '-d' decouple flag
        bool coupledTimes = !IsSet('d');

        Timestamp now = Timestamp::Now();
        offerClass_.from_sec = now.tv_sec;
        offerClass_.from_usec = now.tv_usec;
        offerClass_.to_sec = 0x7fffffff;
        offerClass_.to_usec = 999999;

        ProdSpec spec;
        spec.feedtype = ANY;
        spec.pattern = ".*";

        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), spec.feedtype) != FEEDTYPE_OK) {
                LogError("Bad feedtype \"{}\"", GetOption('f'));
                return false;
            }
        }
        if (IsSet('p')) spec.pattern = GetOption('p');
        offerClass_.specs.push_back(spec);

        // Calculate starting offset using the modern C++ static constant
        Timestamp offsetTs = Timestamp::NONE; 
        if (IsSet('o')) {
            offsetTs.tv_sec = std::stoll(GetOption('o'));
            offsetTs.tv_usec = 0;
        }

        if (coupledTimes && offsetTs.tv_sec > totalTimeo_) {
            LogError("Total timeout %u too small for time-offset %ld", totalTimeo_, static_cast<long>(offsetTs.tv_sec));
            return false;
        }

        if (coupledTimes) {
            if (offsetTs == Timestamp::NONE) {
                offsetTs.tv_sec = totalTimeo_;
                offsetTs.tv_usec = 0;
            }
            offerClass_.from_sec = now.tv_sec - offsetTs.tv_sec;
        } else {
            if (offsetTs == Timestamp::NONE) {
                offerClass_.from_sec = 0; // Start at beginning of queue
            } else {
                offerClass_.from_sec = now.tv_sec - offsetTs.tv_sec;
            }
        }

        return true;
    }

    bool Initialize() override {
        if (!QueueApp::Initialize()) return false;
        LogNotice("Starting Up: pqsend to {}", remoteHost_);

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
        auto sa = ServiceAddr::Parse(remoteHost_, "localhost", port_);
        if (!sa) {
            LogError("Invalid target address: {}", remoteHost_);
            return EXIT_FAILURE;
        }

        auto cursor = pq_->CreateCursor();

        while (!SignalManager::IsDone()) {
            client_ = NetworkFactory::CreateClient(std::move(*sa), rpcTimeout_);
            if (client_->Connect() != 0) {
                LogError("Connection failed: {}", client_->GetLastError());
                if (SignalManager::IsDone()) break;
                sleep(rpcTimeout_);
                continue;
            }

            stats_.nconnects++;
            
            // 1. Handshake Phase
            HiyaRequest req;
            req.offeredClass = offerClass_;
            HiyaResponse resp = client_->SendHiya(req, rpcTimeout_);

            if (resp.statusCode == ReplyStatus::DONT_SEND) {
                LogError("Remote server refused HIYA connection: {}", client_->GetLastError());
                client_->Disconnect();
                sleep(rpcTimeout_);
                continue;
            } else if (resp.statusCode == ReplyStatus::RECLASS) {
                LogNotice("Server narrowed request via RECLASS.");
                wantClass_ = resp.acceptedClass;
            } else {
                wantClass_ = offerClass_;
            }

            // 2. Queue Iteration Phase
            cursor->setCursor(Timestamp(offerClass_.from_sec, offerClass_.from_usec));
            
            while (!SignalManager::IsDone()) {
                int status = cursor->sequence(Match::GreaterThan, offerClass_, send_callback, this);

                if (status == 0) {
                    continue; // Product sent successfully
                } else if (status == static_cast<int>(PqStatus::End)) {
                    LogDebug("End of Queue reached. Flushing.");
                    client_->Flush();
                    
                    if (interval_ == 0) {
                        SignalManager::TriggerShutdown(); // Exit if one-shot mode
                        break;
                    }
                    
                    // Sleep until new products arrive
                    struct timeval tv = { static_cast<time_t>(interval_), 0 };
                    select(0, nullptr, nullptr, nullptr, &tv);
                } else if (status == EAGAIN || status == EACCES) {
                    LogDebug("Queue locked, retrying...");
                    usleep(100000); // 100ms
                } else {
                    LogError("Queue sequence aborted. Reconnecting. (Status: {})", status);
                    break; // Break sequence loop to trigger a reconnect
                }
            }
            
            stats_.ndisco++;
            client_->Disconnect();
        }

        LogNotice("Exiting. Connections: {}, Disconnects: {}, Products Sent: {}", 
                  stats_.nconnects, stats_.ndisco, stats_.nprods);

        return EXIT_SUCCESS;
    }

public:
    PqSendApp() : QueueApp(PqFlags::ReadOnly, "Sends products from a local product queue to an LDM server.") {}
};

int main(int argc, char *argv[]) {
    PqSendApp app;
    return app.Execute(argc, argv);
}
