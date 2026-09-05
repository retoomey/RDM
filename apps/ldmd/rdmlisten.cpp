#include "config.h"
#include "Application.h"
#include "ConfParser.h"
#include "AclManager.h"
#include "ProcessManager.h"
#include "ProcessUtil.h"
#include "NetworkUtils.h"
#include "Registry.h"
#include "Log.h"
#include "SignalManager.h"
#include "ServiceAddr.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <memory>

using namespace rdm;

class RdmListenApp : public Application {
private:
    std::string ldmBindAddr_;
    unsigned ldmPort_{388};
    unsigned maxClients_{256};
    std::string configPath_;
    ProcessManager processManager_;

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterOption('I', "interface", "Use network interface address", "");
        RegisterOption('P', "port", "Port number (default 388)", "388");
        RegisterOption('M', "maxclients", "Max client limit", "256");
        RegisterOption('q', "queue", "Product-queue pathname", "");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;
        ldmBindAddr_ = GetOption('I');
        if (IsSet('P')) ldmPort_ = std::stoul(GetOption('P'));
        if (IsSet('M')) maxClients_ = std::stoul(GetOption('M'));
        if (IsSet('q')) registry::setQueuePath(GetOption('q'));
        configPath_ = positionalArgs_.empty() ? registry::getLdmdConfigPath() : positionalArgs_[0];
        return true;
    }

    int Run() override {
        ServerConfig config;
        if (!ConfParser::Parse(configPath_, config, ldmPort_)) {
            LogFatal("rdm-listen failed to parse {}", configPath_);
            return EXIT_FAILURE;
        }

        AclManager aclManager(std::move(config.allowRules), std::move(config.acceptRules));
        if (!aclManager.RequiresServer()) {
            LogNotice("No ALLOW or ACCEPT rules found. rdm-listen exiting cleanly.");
            return EXIT_SUCCESS;
        }

        ServiceAddr target(ldmBindAddr_.empty() ? "::" : ldmBindAddr_, ldmPort_);
        struct sockaddr_storage addr;
        socklen_t len;
        if (!target.Resolve(&addr, &len, AF_UNSPEC, true)) {
            target = ServiceAddr(ldmBindAddr_.empty() ? "0.0.0.0" : ldmBindAddr_, ldmPort_);
            if (!target.Resolve(&addr, &len, AF_INET, true)) return EXIT_FAILURE;
        }

        int sock = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) return EXIT_FAILURE;
        os::ensureCloseOnExec(sock);

        int on = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (addr.ss_family == AF_INET6) {
            int no = 0;
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
        }

        PrivilegeManager::Instance().RaisePrivileges();
        if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), len) < 0 || listen(sock, 1024) != 0) {
            LogSyserr("rdm-listen failed to bind/listen on port {}", ldmPort_);
            close(sock);
            return EXIT_FAILURE;
        }
        PrivilegeManager::Instance().LowerPrivileges();

        LogNotice("rdm-listen bound to port {}. Awaiting connections.", ldmPort_);

        while (!SignalManager::IsDone()) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            struct timeval tv{6, 0};

            int ready = select(sock + 1, &readfds, nullptr, nullptr, &tv);
            if (ready < 0) {
                if (errno != EINTR) break;
                continue;
            } else if (ready > 0) {
                struct sockaddr_storage raddr{};
                socklen_t rlen = sizeof(raddr);
                int clientSock = accept(sock, reinterpret_cast<struct sockaddr*>(&raddr), &rlen);
                if (clientSock < 0) continue;

                os::ensureCloseOnExec(clientSock);
                setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

                std::string remoteHost = network::GetHostByAddr(&raddr, rlen);
std::string remoteIp = network::GetIpString(&raddr, rlen); // Replaced GetHostByAddr

if (processManager_.Count() >= maxClients_ || !aclManager.IsHostOk(remoteHost, remoteIp)) {
    LogNotice("Denying connection from [{}]", remoteHost);
    close(clientSock);
    continue;
}

                char fdStr[32], portStr[32];
                std::snprintf(fdStr, sizeof(fdStr), "%d", clientSock);
                std::snprintf(portStr, sizeof(portStr), "%u", ldmPort_);

                std::vector<const char*> args = {"rdm-accept", "-S", fdStr, "-P", portStr};
                std::string qPath = registry::getQueuePath();
                if (!qPath.empty()) { args.push_back("-q"); args.push_back(qPath.c_str()); }
                args.push_back(configPath_.c_str());
                args.push_back(nullptr);

                os::ExecParams params;
                params.argv = const_cast<char**>(args.data());
                params.preserveFd = clientSock;
                params.resetSignals = true;

                pid_t pid = os::ForkAndExec(params);
                if (pid > 0) processManager_.Add(pid, "rdm-accept [" + remoteHost + "]");
                
                close(clientSock);
            }
            while (processManager_.Reap(-1, WNOHANG) > 0) {}
        }

        processManager_.KillAll(SIGTERM);
        while (processManager_.Count() > 0) processManager_.Reap(-1, 0);
        close(sock);
        return EXIT_SUCCESS;
    }

public:
    RdmListenApp() : Application("The RDM Port Listener.") {}
};

int main(int argc, char* argv[]) {
    RdmListenApp app;
    return app.Execute(argc, argv);
}
