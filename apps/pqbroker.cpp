#include "config.h"
#include "Application.h"
#include "IServer.h"
#include "NetworkFactory.h"
#include "ConfParser.h"
#include "AclManager.h"
#include "ULDB.h"
#include "UpstreamClientHandler.h"
#include "ProcessManager.h"
#include <memory>

using namespace rdm;

/** This separates the port listener tree from the ldmd supervisor,
 * which makes the allow/accept match the exec pattern of calling pqact,
 * pqsend, etc. */
class PqBrokerApp : public Application {
private:
    std::string ldmBindAddr_;
    unsigned ldmPort_{388};
    unsigned maxClients_{256};
    std::string configPath_;
    Uldb uldb_;
    ProcessManager processManager_;
    std::unique_ptr<IServer> server_;

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterOption('I', "interface", "Use network interface address", "");
        RegisterOption('P', "port", "Port number (default 388)", "388");
        RegisterOption('M', "maxclients", "Max client limit", "256");
        
        // ADDED: Accept queue path and Nagle's algorithm flags from ldmd
        RegisterOption('q', "queue", "Product-queue pathname", "");
        RegisterFlag('N', "Disable Nagle's Algorithm");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;
        
        ldmBindAddr_ = GetOption('I');
        if (IsSet('P')) ldmPort_ = std::stoul(GetOption('P'));
        if (IsSet('M')) maxClients_ = std::stoul(GetOption('M'));
        
        // ADDED: Register the queue path with the system registry so ULDB can find it
        if (IsSet('q')) registry::setQueuePath(GetOption('q'));
        
        if (!positionalArgs_.empty()) {
            configPath_ = positionalArgs_[0];
        } else {
            configPath_ = registry::getLdmdConfigPath();
        }
        return true;
    }

    int Run() override {
        ServerConfig config = ConfParser::Parse(configPath_, ldmPort_);
        auto aclManager = std::make_unique<AclManager>(std::move(config.allowRules), std::move(config.acceptRules));

        if (!aclManager->RequiresServer()) {
            LogNotice("No ALLOW or ACCEPT rules found. pqbroker exiting cleanly.");
            return EXIT_SUCCESS;
        }

        if (uldb_.Open("") != UldbStatus::SUCCESS) {
            LogFatal("pqbroker failed to attach to the ULDB. Is ldmd running?");
            return EXIT_FAILURE;
        }

        server_ = NetworkFactory::CreateServer();
        auto handler = std::make_shared<UpstreamServerHandler>(*aclManager, uldb_, processManager_);

        // Start() blocks and runs the RPC loop until SignalManager triggers shutdown
        if (server_->Start(ldmBindAddr_, ldmPort_, maxClients_, handler, processManager_) != 0) {
            return EXIT_FAILURE;
        }

        // ==========================================================
        // BULLETPROOF CLEANUP LOGIC
        // ==========================================================
        LogNotice("pqbroker shutting down: signaling children and waiting for termination...");
        
        // 1. Ignore SIGTERM so pqbroker isn't killed while cleaning up its own mess
        SignalManager::Ignore(SIGTERM); 
        
        // 2. Explicitly send SIGTERM to all active socket connections
        processManager_.KillAll(SIGTERM);

        // 3. Block and reap until the process manager is entirely empty
        while (processManager_.Count() > 0) {
            processManager_.Reap(-1, 0); 
        }
        
        LogNotice("pqbroker shutdown complete. All socket children reaped.");
        // ==========================================================

        uldb_.Close();
        return EXIT_SUCCESS;
    }
public:
    PqBrokerApp() : Application("The LDM Port 388 Listening Server.") {}
};

int main(int argc, char* argv[]) {
    PqBrokerApp app;
    return app.Execute(argc, argv);
}
