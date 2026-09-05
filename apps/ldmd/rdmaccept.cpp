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

class RdmAcceptApp : public Application {
private:
    unsigned ldmPort_{388};
    std::string configPath_;
    Uldb uldb_;
    ProcessManager processManager_;
    std::unique_ptr<IServer> server_;
    int socketFd_{-1};

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterOption('P', "port", "Port number (default 388)", "388");
        RegisterOption('q', "queue", "Product-queue pathname", "");
        RegisterOption('S', "socket-fd", "Inherited socket FD", "-1");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;
        if (IsSet('P')) ldmPort_ = std::stoul(GetOption('P'));
        if (IsSet('q')) registry::setQueuePath(GetOption('q'));
        if (IsSet('S')) socketFd_ = std::stoi(GetOption('S'));
        
        if (socketFd_ < 0) {
            LogError("rdm-accept must be spawned with a valid inherited socket (-S)");
            return false;
        }

        configPath_ = positionalArgs_.empty() ? registry::getLdmdConfigPath() : positionalArgs_[0];
        return true;
    }

    int Run() override {
        ServerConfig config;
        if (!ConfParser::Parse(configPath_, config, ldmPort_)) {
            LogFatal("rdm-accept failed to parse {}", configPath_);
            return EXIT_FAILURE;
        }

        AclManager aclManager(std::move(config.allowRules), std::move(config.acceptRules));
        if (uldb_.Open("") != UldbStatus::SUCCESS) {
            LogFatal("rdm-accept failed to attach to the ULDB.");
            return EXIT_FAILURE;
        }

        server_ = NetworkFactory::CreateServer();
        auto handler = std::make_shared<UpstreamServerHandler>(aclManager, uldb_, processManager_);

        if (server_->StartWithSocket(socketFd_, handler, processManager_) != 0) {
            LogError("Server failed to start on inherited socket");
            uldb_.Close();
            return EXIT_FAILURE;
        }

        uldb_.Close();
        return EXIT_SUCCESS;
    }

public:
    RdmAcceptApp() : Application("The RDM Inbound Session Handler.") {}
};

int main(int argc, char* argv[]) {
    RdmAcceptApp app;
    return app.Execute(argc, argv);
}
