#pragma once
#include "IServer.h"
#include "PeerContext.h"
#include <atomic>
#include <string>
#include <memory>
#include <netinet/in.h> // Explicitly required for sockaddr_in

namespace rdm {

class SunRpcServer : public IServer {
public:
    SunRpcServer();
    ~SunRpcServer() override;

    int Start(const std::string& ip_addr, unsigned int port, unsigned int max_clients,
       std::shared_ptr<IServiceHandler> handler, ProcessManager& procMgr) override;
    void Stop() override;

    static std::shared_ptr<IServiceHandler> GetCurrentHandler();

    static void SetCurrentHandler(std::shared_ptr<IServiceHandler> handler);

private:
    std::atomic<bool> is_running_{false};
    int server_socket_{-1};
    std::shared_ptr<IServiceHandler> handler_;
};

}
