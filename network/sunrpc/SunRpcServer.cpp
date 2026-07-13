#include "SunRpcServer.h"
#include "SunRpcClient.h"
#include "SunRpcXdr.h"
#include "Registry.h"
#include "RpcTypes.h"
#include "SunRpcDispatcher.h"
#include "config.h"

#ifndef LDM_SELECT_TIMEO
    #define LDM_SELECT_TIMEO  6
#endif

#include "Log.h"
#include "ProcessUtil.h"
#include "ProcessManager.h"
#include "IProductStore.h"
#include "NetworkUtils.h"
#include "PrivilegeManager.h"
#include "ServiceAddr.h"
#include "SignalManager.h"

#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#define SIX 6
#define HEREIS 1
#define FEEDME 4
#define HIYA 5
#define NOTIFICATION 8
#define NOTIFYME 9
#define COMINGSOON 12
#define BLKDATA 13
#define IS_ALIVE 14

using namespace rdm::os;
using namespace rdm::registry;

namespace rdm {

static std::shared_ptr<IServiceHandler> g_current_handler;
static thread_local PeerContext tl_current_peer;

std::shared_ptr<IServiceHandler> SunRpcServer::GetCurrentHandler() {
    return g_current_handler;
}

void SunRpcServer::SetCurrentHandler(std::shared_ptr<IServiceHandler> handler) {
    g_current_handler = handler;
}

namespace {
    static unsigned int GetSocketBufferSize(int sock, int optname) {
        int optval = 0;
        socklen_t optlen = sizeof(optval);
        if (getsockopt(sock, SOL_SOCKET, optname, reinterpret_cast<void*>(&optval), &optlen) < 0) {
            return 0;
        }

        if (optval < 4096) {
            optval = MAX_RPC_BUF_NEEDED;
            (void)setsockopt(sock, SOL_SOCKET, optname, reinterpret_cast<const char*>(&optval), optlen);
        }
        return static_cast<unsigned int>(optval);
    }

    static void InitializeActivePeer(const struct sockaddr_storage* raddr, socklen_t addrLen, int sock) {
        struct sockaddr_storage norm_addr = *raddr;
        socklen_t norm_len = addrLen;

        if (norm_addr.ss_family == AF_INET6) {
            auto* sin6 = reinterpret_cast<struct sockaddr_in6*>(&norm_addr);
            if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
                struct sockaddr_in sin4{};
                sin4.sin_family = AF_INET;
                sin4.sin_port = sin6->sin6_port;
                std::memcpy(&sin4.sin_addr.s_addr, &sin6->sin6_addr.s6_addr[12], 4);
                std::memcpy(&norm_addr, &sin4, sizeof(sin4));
                norm_len = sizeof(sin4);
            }
        }

        tl_current_peer.addr = norm_addr;
        tl_current_peer.addrLen = norm_len;

        char ipStr[NI_MAXHOST];
        if (getnameinfo(reinterpret_cast<const struct sockaddr*>(&norm_addr), norm_len, ipStr, sizeof(ipStr), nullptr, 0, NI_NUMERICHOST) == 0) {
            if (norm_addr.ss_family == AF_INET6) {
                tl_current_peer.ip_string = "[" + std::string(ipStr) + "]";
            } else {
                tl_current_peer.ip_string = ipStr;
            }
        } else {
            tl_current_peer.ip_string = "unknown_ip";
        }

        tl_current_peer.hostname = network::GetHostByAddr(&norm_addr, norm_len);
        tl_current_peer.send_size = GetSocketBufferSize(sock, SO_SNDBUF);
        tl_current_peer.recv_size = GetSocketBufferSize(sock, SO_RCVBUF);

        LogInfo("RPC buffer sizes for {}: send={}; recv={}",
                 tl_current_peer.hostname, tl_current_peer.send_size, tl_current_peer.recv_size);
    }

    void server_dispatcher_6(struct svc_req* rqstp, SVCXPRT* transp) {
        auto handler = SunRpcServer::GetCurrentHandler();
        if (!handler) {
            svcerr_systemerr(transp);
            return;
        }
        const PeerContext& peer = tl_current_peer;

        // 1. Delegate to the unified Data Plane
        RpcDispatchResult dataResult = DispatchDataPlaneRpc(rqstp, transp, handler, peer);
        if (dataResult == RpcDispatchResult::FatalError) {
            svc_destroy(transp);
            exit(1);
        } else if (dataResult == RpcDispatchResult::Handled) {
            return; // The data-plane helper successfully managed the request
        }

        // 2. Handle Server-Side Control Plane
        switch (rqstp->rq_proc) {
            case NULLPROC:
                svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_void), nullptr);
                return;

            case FEEDME: {
                FeedParNet fpar;
                fpar.max_hereis = 0;
                if (!svc_getargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_feedpar), reinterpret_cast<char*>(&fpar))) {
                    svcerr_decode(transp);
                    return;
                }

                FeedRequest req;
                req.isNotifier = false;
                req.maxHereis = fpar.max_hereis;
                req.requestedClass = fpar.clss;
                FeedResponse resp = handler->OnFeedRequest(peer, req);

                if (!svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_net_fornme_reply), reinterpret_cast<char*>(&resp))) {
                    LogError("svc_sendreply(...) failure");
                    svcerr_systemerr(transp);
                    exit(1);
                }

                svc_freeargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_feedpar), reinterpret_cast<char*>(&fpar));

                if (resp.statusCode == ReplyStatus::OK) {
                    int new_sock = dup(transp->xp_sock);
                    svc_destroy(transp);
                    uint16_t port = (peer.addr.ss_family == AF_INET6) ?
                        ntohs(reinterpret_cast<const struct sockaddr_in6*>(&peer.addr)->sin6_port) :
                        ntohs(reinterpret_cast<const struct sockaddr_in*>(&peer.addr)->sin_port);
                    
                    ServiceAddr target(peer.ip_string, port);
                    auto client = std::make_shared<SunRpcClient>(std::move(target), new_sock, &peer.addr, 60);
                    client->SetMaxHereIs(fpar.max_hereis);
                    int status = handler->StreamProducts(client);
                    exit(status);
                } else {
                    svc_destroy(transp);
                    exit(1);
                }
                return;
            }

            case NOTIFYME: {
                ProdClass want;
                if (!svc_getargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_prod_class), reinterpret_cast<char*>(&want))) {
                    svcerr_decode(transp);
                    return;
                }

                FeedRequest req;
                req.isNotifier = true;
                req.maxHereis = 0;
                req.requestedClass = want;
                FeedResponse resp = handler->OnFeedRequest(peer, req);

                if (!svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_net_fornme_reply), reinterpret_cast<char*>(&resp))) {
                    LogError("svc_sendreply(...) failure");
                    svcerr_systemerr(transp);
                    exit(1);
                }

                svc_freeargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_prod_class), reinterpret_cast<char*>(&want));

                if (resp.statusCode == ReplyStatus::OK) {
                    int new_sock = dup(transp->xp_sock);
                    svc_destroy(transp);
                    uint16_t port = (peer.addr.ss_family == AF_INET6) ?
                        ntohs(reinterpret_cast<const struct sockaddr_in6*>(&peer.addr)->sin6_port) :
                        ntohs(reinterpret_cast<const struct sockaddr_in*>(&peer.addr)->sin_port);
                    
                    ServiceAddr target(peer.ip_string, port);
                    auto client = std::make_shared<SunRpcClient>(std::move(target), new_sock, &peer.addr, 60);
                    int status = handler->StreamProducts(client);
                    exit(status);
                } else {
                    svc_destroy(transp);
                    exit(1);
                }
                return;
            }

            case IS_ALIVE: {
                u_int id = 0;
                if (!svc_getargs(transp, reinterpret_cast<xdrproc_t>(xdr_u_int), reinterpret_cast<char*>(&id))) {
                    svcerr_decode(transp);
                    return;
                }

                bool_t alive = handler->IsAlive(id) ? TRUE : FALSE;
                if (!svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_bool), reinterpret_cast<char*>(&alive))) {
                    svcerr_systemerr(transp);
                    exit(1);
                }

                svc_freeargs(transp, reinterpret_cast<xdrproc_t>(xdr_u_int), reinterpret_cast<char*>(&id));
                svc_destroy(transp);
                exit(0);
                return;
            }

            case HIYA: {
                ProdClass offered;
                if (!svc_getargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_prod_class), reinterpret_cast<char*>(&offered))) {
                    svcerr_decode(transp);
                    return;
                }

                HiyaRequest req;
                req.offeredClass = offered;
                HiyaResponse resp = handler->OnHiyaRequest(peer, req);

                if (!svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_net_hiya_reply), reinterpret_cast<char*>(&resp))) {
                    svcerr_systemerr(transp);
                }

                svc_freeargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_prod_class), reinterpret_cast<char*>(&offered));
                return;
            }

            default:
                svcerr_noproc(transp);
                return;
        }
    }

    int create_ldm_tcp_svc(int* sockp, const std::string& bindIp, unsigned localPort) {
        int error = 0;

        ServiceAddr target(bindIp.empty() ? "::" : bindIp, localPort);
        struct sockaddr_storage addr;
        socklen_t len;
        
        if (!target.Resolve(&addr, &len, AF_UNSPEC, true)) {
            target = ServiceAddr(bindIp.empty() ? "0.0.0.0" : bindIp, localPort);
            if (!target.Resolve(&addr, &len, AF_INET, true)) {
                LogSyserr("Failed to resolve bind address");
                return EINVAL;
            }
        }

        int sock = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) return errno;

        os::ensureCloseOnExec(sock);

        if (addr.ss_family == AF_INET6) {
            int no = 0;
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
        }

        int on = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        PrivilegeManager::Instance().RaisePrivileges();
        
        if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), len) < 0) {
            error = errno;
            LogSyserr("Couldn't bind to port {}", localPort);
        } else if (listen(sock, 1024) != 0) {
            error = errno;
            LogSyserr("Couldn't listen on socket");
        } else {
            *sockp = sock;
        }
        
        PrivilegeManager::Instance().LowerPrivileges();

        if (error) close(sock);
        return error;
    }

    int runSvc(SVCXPRT* const xprt, const char* const hostId) {
        LogDebug("Entered RPC Service Loop");

        const unsigned TIMEOUT = 4 * getSystemInterval();
        const int sock = xprt->xp_sock;

        if (sock < 0 || sock >= FD_SETSIZE) {
            LogError("Socket descriptor {} exceeds select() limits (FD_SETSIZE={}).", sock, FD_SETSIZE);
            return EBADF;
        }

        int status = 0;
        for (;;) {
            if (SignalManager::IsDone()) {
              status = 0;
              break;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);

            struct timeval tv{};
            tv.tv_sec = TIMEOUT;
            tv.tv_usec = 0;

            int ready = select(sock + 1, &readfds, nullptr, nullptr, TIMEOUT != 0 ? &tv : nullptr);
            if (SignalManager::IsDone()) break;

            if (ready < 0) {
                if (errno == EINTR) {
                    LogDebug("select() was interrupted");
                    continue;
                }
                LogSyserr("select() failure on socket {}", sock);
                status = errno;
                break;
            }
            if (ready == 0) {
                LogDebug("Timeout");
                status = ETIMEDOUT;
                break;
            }

            svc_getreqset(&readfds);
            if (SignalManager::IsDone()) break;

            if (!FD_ISSET(sock, &svc_fdset)) {
                LogInfo("RPC layer closed connection on socket {}. Connection with client LDM {} has been lost.", sock, hostId);
                status = ECONNRESET;
                break;
            }
            
            LogDebug("RPC message processed");
        }

        if (status == ETIMEDOUT){
          LogDebug("Client LDM, {}, has been silent for {} seconds", hostId, TIMEOUT);
        }
        if (status != ECONNRESET) {
          svc_destroy(xprt);
        }

        LogDebug("Returning from RPC Service Loop");
        return status;
    }

    static int runChildLdm(const struct sockaddr_storage* raddr, socklen_t addrLen, const int xp_sock) {
        InitializeActivePeer(raddr, addrLen, xp_sock);

        int status;
        PeerContext* remote = &tl_current_peer;

        SVCXPRT* xprt = svcfd_create(xp_sock, remote->send_size, remote->recv_size);
        if (xprt == nullptr) {
            LogError("Can't create fd service.");
            status = EFAULT;
        } else {
            xprt->xp_raddr = {};
            std::memcpy(&xprt->xp_raddr, raddr, std::min(static_cast<size_t>(addrLen), sizeof(xprt->xp_raddr)));
            xprt->xp_addrlen = sizeof(xprt->xp_raddr);

            auto handler = SunRpcServer::GetCurrentHandler();
            if (handler && handler->IsConnectionAllowed(remote->hostname, remote->ip_string)) {
                status = 0;
            } else {
                LogWarning("Denying connection from \"{}\" because not allowed", remote->hostname);
                svcerr_weakauth(xprt);
                status = ESRCH;
            }

            if (status == 0) {
                LogInfo("Connection from {}", remote->hostname);
                
                xprt->xp_raddr = {};

                std::memcpy(&xprt->xp_raddr, raddr, std::min(static_cast<size_t>(addrLen), sizeof(xprt->xp_raddr)));
                xprt->xp_addrlen = sizeof(*raddr);
                
                if (!svc_register(xprt, LDM_PROG, SIX, reinterpret_cast<void (*)(struct svc_req*, SVCXPRT*)>(server_dispatcher_6), 0)) {
                    LogError("Unable to register LDM-6 service.");
                    status = EFAULT;
                }
                
                if (status == 0) {
                    status = runSvc(xprt, remote->hostname.c_str());
                    xprt = nullptr;
                    if (status == ECONNRESET) status = 0;
                }
            }
            if (xprt) svc_destroy(xprt);
        }
        return status;
    }

    void handle_connection(const int sock, unsigned int max_clients, 
      ProcessManager& procMgr) {
        struct sockaddr_storage raddr{};
        socklen_t len;
        int xp_sock;
        pid_t pid;

    again:
        len = sizeof(raddr);
        raddr = {};
        xp_sock = accept(sock, reinterpret_cast<struct sockaddr*>(&raddr), &len);

        if (SignalManager::IsDone()) {
            if (xp_sock >= 0) close(xp_sock);
            return;
        }

        if (xp_sock < 0) {
            if (errno == EINTR) {
                errno = 0;
                goto again;
            }
            LogSyserr("accept() failure: sock={}", sock);
            return;
        }

        ensureCloseOnExec(xp_sock);

        if (procMgr.Count() >= max_clients) {
            InitializeActivePeer(&raddr, len, xp_sock);
            LogNotice("Denying connection from [{}] because too many clients", tl_current_peer.ip_string);
            (void)close(xp_sock);
            return;
        }

        pid = ldmFork();
        if (pid == -1) {
            LogError("Couldn't fork process to handle incoming connection");
            (void)close(xp_sock);
            return;
        }

        if (pid > 0) {
            // Parent: track the client process
            (void)close(xp_sock);
            procMgr.Add(pid, std::string("RPC Client"));
            return;
        }

        // Child: handle the connection
        (void)close(sock);
        int status = runChildLdm(&raddr, len, xp_sock);
        exit(status);
    }

    void sock_svc(const int sock, std::atomic<bool>& is_running, 
        unsigned int max_clients, ProcessManager& procMgr) {
        const int width = sock + 1;
        while (is_running && !SignalManager::IsDone()) {
            int ready;
            fd_set readfds;
            struct timeval stimeo;
            stimeo.tv_sec = LDM_SELECT_TIMEO;
            stimeo.tv_usec = 0;

            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);

            ready = select(width, &readfds, 0, 0, &stimeo);
            if (SignalManager::IsDone()) break;

            if (ready < 0) {
                if (errno != EINTR) {
                    LogSyserr("select() failure: sock={}", sock);
                    SignalManager::TriggerShutdown();
                    break;
                }
            } else if (ready > 0) {
                handle_connection(sock, max_clients, procMgr);
            }

            // Clean up any finished client processes
            auto handler = SunRpcServer::GetCurrentHandler();
            if (handler) {
                while (handler->ReapChildProcess() > 0) {}
            }
        }
    }
}

SunRpcServer::SunRpcServer() = default;

SunRpcServer::~SunRpcServer() {
    Stop();
}

int SunRpcServer::Start(const std::string& ip_addr, unsigned int port, unsigned int max_clients,
     std::shared_ptr<IServiceHandler> handler, ProcessManager& procMgr) {
    handler_ = std::move(handler);
    g_current_handler = handler_;
    int sock = -1;
    
    int status = create_ldm_tcp_svc(&sock, ip_addr, port);
    if (status != 0) {
        return status;
    }

    server_socket_ = sock;
    is_running_ = true;

    // --- ADDED READINESS NOTIFICATION FOR SUPERVISOR COOPERATION ---
    fmt::print(stdout, "[RDM_READY] Listening on port {}\n", port);
    std::fflush(stdout);

    sock_svc(server_socket_, is_running_, max_clients, procMgr);

    return 0;
}

void SunRpcServer::Stop() {
    is_running_ = false;
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    g_current_handler.reset();
}

}
