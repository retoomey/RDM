#include "SunRpcClient.h"
#include "SunRpcServer.h"
#include "SunRpcXdr.h"
#include "Registry.h"
#include "RpcTypes.h"
#include "Log.h"
#include "SignalManager.h"
#include "PeerContext.h"
#include "IProductStore.h"
#include "SunRpcDispatcher.h"

#include <cstring>
#include <cstdlib>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/tcp.h>

namespace rdm {

static thread_local std::shared_ptr<IServiceHandler> tl_client_handler;
static thread_local PeerContext tl_client_peer;

namespace {
    void client_dispatcher_6(struct svc_req* rqstp, SVCXPRT* transp) {
        if (!tl_client_handler) {
            svcerr_systemerr(transp);
            return;
        }
        const PeerContext& peer = tl_client_peer;

        // 1. Delegate to the unified Data Plane
        RpcDispatchResult dataResult = DispatchDataPlaneRpc(rqstp, transp, tl_client_handler, peer);
        if (dataResult == RpcDispatchResult::FatalError) {
            svc_destroy(transp);
            exit(1);
        } else if (dataResult == RpcDispatchResult::Handled) {
            return; // The data-plane helper successfully managed the request
        }

        // 2. Handle Client-Side Control Plane
        switch (rqstp->rq_proc) {
            case NULLPROC:
                svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_void), nullptr);
                return;
                
            default:
                svcerr_noproc(transp);
                return;
        }
    }
}

SunRpcClient::SunRpcClient(ServiceAddr target, unsigned int timeout_sec)
    : target_(std::move(target)), timeout_sec_(timeout_sec), clnt_(nullptr) {}

SunRpcClient::SunRpcClient(ServiceAddr target, int existing_socket, const struct sockaddr_storage* remote_addr, unsigned int timeout_sec)
    : target_(std::move(target)), timeout_sec_(timeout_sec), existing_socket_(existing_socket), clnt_(nullptr) {
    if (remote_addr) {
        std::memcpy(&remote_addr_, remote_addr, sizeof(struct sockaddr_storage));
        has_remote_addr_ = true;
    }
}

SunRpcClient::~SunRpcClient() {
    Disconnect();
}

int SunRpcClient::Connect() {
    if (existing_socket_ >= 0 && has_remote_addr_) {
        struct netbuf nb;
        socklen_t actual_len = (remote_addr_.ss_family == AF_INET6) ?
                                sizeof(struct sockaddr_in6) :
                                sizeof(struct sockaddr_in);
        nb.maxlen = actual_len;
        nb.len = actual_len;
        nb.buf = &remote_addr_;

        clnt_ = clnt_vc_create(existing_socket_, &nb, LDM_PROG, SIX, 262216, 0);
        if (clnt_ == nullptr) {
            struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(&remote_addr_);
            clnt_ = clnttcp_create(sin, LDM_PROG, SIX, &existing_socket_, 262216, 0);
        }
        
        if (clnt_ == nullptr) return -1;
        return 0;

    } else {
        struct sockaddr_storage addrStorage;
        socklen_t addrLen = 0;
        
        if (!target_.Resolve(&addrStorage, &addrLen)) {
            return -1;
        }

        int sck = -1;
        int connect_status = -1;
        int retries = (timeout_sec_ > 0 ? timeout_sec_ : 60) * 10;

        for (int i = 0; i < retries; ++i) {
            sck = socket(addrStorage.ss_family, SOCK_STREAM, IPPROTO_TCP);
            if (sck < 0) return -1;

            int flags = fcntl(sck, F_GETFL, 0);
            fcntl(sck, F_SETFL, flags | O_NONBLOCK);

            int res = connect(sck, reinterpret_cast<struct sockaddr*>(&addrStorage), addrLen);

            if (res == 0) {
                fcntl(sck, F_SETFL, flags);
                connect_status = 0;
                break;
            }

            if (res < 0 && errno == EINPROGRESS) {
                fd_set wset;
                FD_ZERO(&wset);
                FD_SET(sck, &wset);

                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 100000;

                res = select(sck + 1, nullptr, &wset, nullptr, &tv);
                if (res > 0) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt(sck, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err == 0) {
                        fcntl(sck, F_SETFL, flags);
                        connect_status = 0;
                        break;
                    }
                }
            }

            close(sck);
            usleep(100000); 
        }

        if (connect_status < 0) {
            return -1;
        }

        struct netbuf nb;
        nb.maxlen = addrLen;
        nb.len = addrLen;
        nb.buf = &addrStorage;

        clnt_ = clnt_vc_create(sck, &nb, LDM_PROG, SIX, 0, 0);
        if (!clnt_) {
            struct sockaddr_in dummy{};
            dummy.sin_family = AF_INET;
            dummy.sin_port = htons(target_.GetPort());
            clnt_ = clnttcp_create(&dummy, LDM_PROG, SIX, &sck, 0, 0);
        }

        if (clnt_) {
            existing_socket_ = sck;
            struct timeval tv = {0, 0};
            clnt_control(clnt_, CLSET_TIMEOUT, reinterpret_cast<char*>(&tv));
            return 0;
        }

        close(sck);
        return -1;
    }
}

void SunRpcClient::Disconnect() {
    if (clnt_) {
        clnt_destroy(clnt_);
        clnt_ = nullptr;
    }
    existing_socket_ = -1;
}

int SunRpcClient::SendProduct(const Product& clean_prod) {
    if (clean_prod.info.sz <= max_hereis_) {
        static bool first_hereis = true;
        if (first_hereis) {
            LogNotice("DEBUG: Firing first HEREIS (size {} <= threshold {})",
                      clean_prod.info.sz, max_hereis_);
            first_hereis = false;
        }
        return CallRpc<Product, void>(
            HEREIS,
            reinterpret_cast<xdrproc_t>(xdr_net_product),
            const_cast<Product*>(&clean_prod),
            reinterpret_cast<xdrproc_t>(xdr_void),
            nullptr
        );
    }
    
    static bool first_comingsoon = true;
    if (first_comingsoon) {
        LogNotice("DEBUG: Firing first COMINGSOON/BLKDATA sequence (size {} > threshold {})",
                  clean_prod.info.sz, max_hereis_);
        first_comingsoon = false;
    }
    
    ComingSoonArgsNet args;
    args.info = clean_prod.info;
    args.pktsz = clean_prod.info.sz;
    int replyCode = 0;
    
    int status = CallRpc<ComingSoonArgsNet, int>(
        COMINGSOON,
        reinterpret_cast<xdrproc_t>(xdr_net_comingsoon_args),
        &args,
        reinterpret_cast<xdrproc_t>(xdr_int),
        &replyCode,
        60
    );
    
    // Only send BLKDATA if the RPC succeeded AND the downstream server wants it (replyCode == 0)
    if (status == 0 && replyCode == 0) {
        DataPktNet dpkp;
        std::memcpy(dpkp.signaturep, clean_prod.info.signature.data(), 16);
        dpkp.pktnum = 0;
        dpkp.dbuf_val = const_cast<char*>(reinterpret_cast<const char*>(clean_prod.data));
        dpkp.dbuf_len = clean_prod.info.sz;
        
        status = CallRpc<DataPktNet, void>(
            BLKDATA,
            reinterpret_cast<xdrproc_t>(xdr_net_datapkt),
            &dpkp,
            reinterpret_cast<xdrproc_t>(xdr_void),
            nullptr
        );
    }
    
    // Always return the RPC transaction status (0 on success), NOT the internal LDM replyCode.
    return status;
}

int SunRpcClient::SendNotification(const ProdInfo& clean_info) {
    return CallRpc<ProdInfo, void>(
        NOTIFICATION,
        reinterpret_cast<xdrproc_t>(xdr_net_prod_info),
        const_cast<ProdInfo*>(&clean_info),
        reinterpret_cast<xdrproc_t>(xdr_void),
        nullptr
    );
}

int SunRpcClient::Flush() {
    return CallRpc<void, void>(
        NULLPROC,
        reinterpret_cast<xdrproc_t>(xdr_void),
        nullptr,
        reinterpret_cast<xdrproc_t>(xdr_void),
        nullptr,
        10
    );
}

std::string SunRpcClient::GetLastError() const {
    if (!clnt_) return "Client is not connected";
    struct rpc_err e;
    clnt_geterr(clnt_, &e);
    return clnt_sperrno(e.re_status);
}

PingStatus SunRpcClient::Ping(unsigned int timeoutSeconds) {
    PingStatus status;
    status.state = ClientState::NOT_FOUND;
    status.elapsedSeconds = 0.0;
    status.port = 0;

    if (!clnt_ && Connect() != 0) {
        status.errorMessage = "Failed to connect";
        return status;
    }

    struct timeval tv = { static_cast<long>(timeoutSeconds), 0 };
    clnt_control(clnt_, CLSET_TIMEOUT, reinterpret_cast<char*>(&tv));

    struct timeval begin, end;
    gettimeofday(&begin, nullptr);

    clnt_call(
        clnt_,
        NULLPROC,
        reinterpret_cast<xdrproc_t>(xdr_void), nullptr,
        reinterpret_cast<xdrproc_t>(xdr_void), nullptr,
        tv
    );

    gettimeofday(&end, nullptr);

    long sec = end.tv_sec - begin.tv_sec;
    long usec = end.tv_usec - begin.tv_usec;
    if (usec < 0) {
        sec--;
        usec += 1000000;
    }

    struct rpc_err e;
    clnt_geterr(clnt_, &e);
    enum clnt_stat rpc_stat = e.re_status;

    status.elapsedSeconds = sec + (usec / 1000000.0);
    status.port = target_.GetPort();

    if (rpc_stat == RPC_SUCCESS) {
        status.state = ClientState::RESPONDING;
        status.errorMessage = "OK";
    } else {
        if (rpc_stat == RPC_TIMEDOUT) status.state = ClientState::TIMEOUT;
        else if (rpc_stat == RPC_UNKNOWNHOST) status.state = ClientState::NOT_FOUND;
        else status.state = ClientState::UNAVAILABLE;
        status.errorMessage = GetLastError();
    }

    return status;
}

FeedResponse SunRpcClient::SubscribeAndListen(const FeedRequest& request, std::shared_ptr<IServiceHandler> handler, unsigned int inactiveTimeoutSecs) {
    FeedResponse response;
    response.statusCode = ReplyStatus::DONT_SEND;

    if (!clnt_) {
        LogError("Client not connected prior to SubscribeAndListen");
        return response;
    }

    struct timeval tv = {30, 0};
    clnt_control(clnt_, CLSET_TIMEOUT, reinterpret_cast<char*>(&tv));

    enum clnt_stat stat;
    if (request.isNotifier) {
        ProdClass* clss_ptr = const_cast<ProdClass*>(&request.requestedClass);
        stat = clnt_call(
            clnt_,
            NOTIFYME,
            reinterpret_cast<xdrproc_t>(xdr_net_prod_class),
            reinterpret_cast<char*>(clss_ptr),
            reinterpret_cast<xdrproc_t>(xdr_net_fornme_reply),
            reinterpret_cast<char*>(&response),
            tv
        );
    } else {
        FeedParNet fpar;
        fpar.clss = request.requestedClass;
        fpar.max_hereis = request.maxHereis;
        
        stat = clnt_call(
            clnt_,
            FEEDME,
            reinterpret_cast<xdrproc_t>(xdr_net_feedpar),
            reinterpret_cast<char*>(&fpar),
            reinterpret_cast<xdrproc_t>(xdr_net_fornme_reply),
            reinterpret_cast<char*>(&response),
            tv
        );
    }

    if (stat != RPC_SUCCESS) {
        LogError("Subscribe RPC failure: {}", clnt_sperrno(stat));
        return response;
    }

    if (response.statusCode == ReplyStatus::OK) {
        int sd = existing_socket_;
        if (sd < 0) {
            LogError("Invalid socket descriptor retrieved from RPC layer: {}", sd);
            return response;
        }

        clnt_control(clnt_, CLSET_FD_NCLOSE, nullptr);
        SVCXPRT* xprt = svcfd_create(sd, 0, 262144);
        
        if (xprt != nullptr) {
            svc_unregister(LDM_PROG, SIX);
            if (svc_register(xprt, LDM_PROG, SIX, reinterpret_cast<void (*)(struct svc_req*, SVCXPRT*)>(client_dispatcher_6), 0)) {
                
                tl_client_handler = handler;
                tl_client_peer.hostname = target_.GetHost();
                tl_client_peer.ip_string = target_.GetHost();
                
                if (has_remote_addr_) {
                    tl_client_peer.addr = remote_addr_;
                    tl_client_peer.addrLen = (remote_addr_.ss_family == AF_INET6) ?
                                              sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
                }

                int loop_status = 0;
                for (;;) {
                    if (SignalManager::IsDone()) {
                      loop_status = 0;
                      break;
                    }

                    fd_set readfds;
                    FD_ZERO(&readfds);
                    FD_SET(sd, &readfds);

                    struct timeval timeout_tv{};
                    timeout_tv.tv_sec = inactiveTimeoutSecs;
                    timeout_tv.tv_usec = 0;

                    int ready = select(sd + 1, &readfds, nullptr, nullptr, inactiveTimeoutSecs != 0 ? &timeout_tv : nullptr);
                    
                    if (ready < 0) {
                        if (errno == EINTR) continue;
                        LogSyserr("select() failure on socket {}", sd);
                        loop_status = errno;
                        break;
                    }

                    if (ready == 0) {
                        loop_status = ETIMEDOUT;
                        break;
                    }

                    svc_getreqset(&readfds);
                    if (!FD_ISSET(sd, &svc_fdset)) {
                        loop_status = ECONNRESET;
                        break;
                    }
                }
                
                tl_client_handler.reset();
                
                if (loop_status == ETIMEDOUT){
                  LogNotice("Connection to upstream timed out");
                }else if (loop_status == ECONNRESET){
                  LogNotice("Connection reset by upstream");
                }else if (loop_status){
                  LogError("Service loop failed: {}", std::strerror(loop_status));
                }

            } else {
                LogError("Failed to register dispatch routine for socket {}", sd);
            }
            svc_unregister(LDM_PROG, SIX);
            svc_destroy(xprt);
        } else {
            LogError("Failed to initialize server-side transport on socket {}", sd);
        }
    } else if (response.statusCode == ReplyStatus::RECLASS) {
        if (response.allowedClass.specs.empty()) {
            LogError("Request denied by upstream LDM");
            response.statusCode = ReplyStatus::SHUTTING_DOWN;
        }
    }
    
    return response;
}

int SunRpcClient::DisableNagles()
{
   if (existing_socket_ < 0) return -1;
   int on = 1;
   return setsockopt(existing_socket_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

}
