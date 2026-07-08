/**
 * @file ldmping.cpp
 * @brief Modernized utility to ping an LDM server.
 */
#include "config.h"
#include "Application.h"
#include "Log.h"
#include "IClient.h"
#include "SignalManager.h"
#include "ServiceAddr.h"
#include "NetworkFactory.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <unistd.h>

using namespace rdm;

struct RemoteTarget {
    std::string host;
    unsigned int port;
    std::unique_ptr<IClient> client;
    PingStatus lastStatus;
};

static const char* state_to_string(ClientState state) {
    switch (state) {
        case ClientState::RESPONDING:   return "RESPONDING";
        case ClientState::NOT_FOUND:    return "NOT_FOUND";
        case ClientState::TIMEOUT:      return "TIMEOUT";
        case ClientState::UNAVAILABLE:  return "UNAVAILABLE";
        case ClientState::SYSTEM_ERROR: return "SYS_ERROR";
        default:                        return "UNKNOWN";
    }
}

class LdmPingApp : public Application {
private:
    unsigned int port_{388};
    unsigned int timeo_{10};
    unsigned int interval_{0};
    bool verbose_{false};
    std::vector<RemoteTarget> targets_;

    void print_label() const {
        LogInfo("{:>10} {:>10} {:>4}    {:<21} {}",
            "State", "Elapsed", "Port", "Remote_Host", "rpc_stat");
    }

    void print_status(const RemoteTarget& target) const {
        if (target.lastStatus.state == ClientState::RESPONDING) {
            LogInfo("{:>10} {:>10.6f} {:>4}   {:<11} {}",
                state_to_string(target.lastStatus.state),
                target.lastStatus.elapsedSeconds,
                target.lastStatus.port,
                target.host,
                target.lastStatus.errorMessage);
        } else {
            LogError("{:>10} {:10.6f} {:4} {:<11} {}",
                state_to_string(target.lastStatus.state),
                target.lastStatus.elapsedSeconds,
                target.lastStatus.port,
                target.host,
                target.lastStatus.errorMessage);
        }
    }

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        
        RegisterFlag('q', "Quiet (suppress normal ping output)");
        RegisterOption('P', "port", "Set the LDM port (default: 388)", "388");
        RegisterOption('t', "timeout", "Set RPC timeout in seconds (default: 10)", "10");
        RegisterOption('i', "interval", "Poll interval in seconds", "");
        RegisterOption('h', "remote", "Remote host to ping", "");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;

        // Default to a 25s loop if running interactively, else single-shot
        verbose_ = isatty(STDERR_FILENO) != 0;
        interval_ = verbose_ ? 25 : 0;

        if (IsSet('v')) verbose_ = true;
        if (IsSet('q')) verbose_ = false;

        if (IsSet('P')) port_ = std::stoul(GetOption('P'));
        if (IsSet('t')) timeo_ = std::stoul(GetOption('t'));
        
        if (IsSet('i')) {
            interval_ = std::stoul(GetOption('i'));
        }

        // Combine hosts provided via the -h flag and any trailing positional arguments
        std::vector<std::string> hostnames;
        if (IsSet('h') && !GetOption('h').empty()) {
            hostnames.push_back(GetOption('h'));
        }
        for (const auto& arg : positionalArgs_) {
            hostnames.push_back(arg);
        }
        if (hostnames.empty()) {
            hostnames.push_back("localhost");
        }

        // Resolve addresses and initialize network clients
        for (const auto& h : hostnames) {
            auto sa = ServiceAddr::Parse(h, "localhost", port_);
            if (sa) {
                RemoteTarget t;
                t.host = sa->GetHost();
                t.port = sa->GetPort();
                t.client = NetworkFactory::CreateClient(std::move(*sa), timeo_);
                targets_.push_back(std::move(t));
            } else {
                LogError("Invalid target address: {}", h);
            }
        }

        return !targets_.empty();
    }

    int Run() override {
        if (verbose_) print_label();

        while (!SignalManager::IsDone()){
            bool allResponding = true;
            
            for (auto& target : targets_) {
                if (!target.client) continue;
                
                target.lastStatus = target.client->Ping(timeo_);
                
                if (target.lastStatus.state != ClientState::RESPONDING) {
                    allResponding = false;
                }
                
                if (verbose_ || target.lastStatus.elapsedSeconds > 1.0 || target.lastStatus.state != ClientState::RESPONDING) {
                    print_status(target);
                }
            }

            if (interval_ == 0) {
                return allResponding ? 0 : 1;
            }
            
            sleep(interval_);
        }
        return 0;
    }

public:
    LdmPingApp() : Application("Checks the status of one or more LDM servers.") {}
};

int main(int argc, char *argv[]) {
    LdmPingApp app;
    return app.Execute(argc, argv);
}
