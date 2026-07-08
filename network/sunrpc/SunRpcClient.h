#pragma once

#include "IClient.h"
#include "ServiceAddr.h"
#include "Log.h"
#include <string>
#include <rpc/rpc.h>

struct sockaddr_in;

namespace rdm {

class SunRpcClient : public IClient {
public:
    SunRpcClient(ServiceAddr target, unsigned int timeout_sec);
    SunRpcClient(ServiceAddr target, int existing_socket, const struct sockaddr_storage* remote_addr, unsigned int timeout_sec);
    ~SunRpcClient() override;

    int DisableNagles() override;

    void SetMaxHereIs(unsigned int max_hereis) override { max_hereis_ = max_hereis; }
    int Connect() override;
    int SendProduct(const Product& product) override;
    int SendNotification(const ProdInfo& info) override;
    int Flush() override;
    void Disconnect() override;
    std::string GetLastError() const override;
    PingStatus Ping(unsigned int timeoutSeconds) override;
    FeedResponse SubscribeAndListen(const FeedRequest& request,
      std::shared_ptr<IServiceHandler> handler, unsigned int inactiveTimeoutSecs) override;

private:
    ServiceAddr target_;
    unsigned int timeout_sec_;
    int existing_socket_{-1};
    struct sockaddr_storage remote_addr_{};
    bool has_remote_addr_{false};
    CLIENT* clnt_{nullptr};
    unsigned int max_hereis_{16384}; 

    template <typename ArgType, typename ResType>
    int CallRpc(unsigned long procNum, xdrproc_t inProc, ArgType* inArg,
                xdrproc_t outProc, ResType* outRes, int timeoutSecs = 0) {
        if (!clnt_) return -1;
        struct timeval tv = { timeoutSecs, 0 };
        enum clnt_stat stat = clnt_call(
            clnt_,
            procNum,
            inProc, reinterpret_cast<char*>(inArg),
            outProc, reinterpret_cast<char*>(outRes),
            tv
        );
        if (stat == RPC_SUCCESS || stat == RPC_TIMEDOUT) return 0;
        LogError("RPC call failed: {}", clnt_sperrno(stat));
        return -1;
    }
};

}
