#include "SunRpcDispatcher.h"
#include "SunRpcXdr.h"
#include "IProductStore.h"
#include "RpcTypes.h"
#include "Log.h"
#include <cstdlib>

namespace rdm {

namespace {
    /**
     * @brief Centralized thread-local scratchpad for HEREIS payloads.
     * Eliminates the 256KB+ memory bloat previously duplicated across client, server, and dispatcher.
     */
    static thread_local std::vector<uint8_t> tl_hereis_scratchpad;

    /**
     * @brief RAII Guard for managing Sun RPC argument memory allocations.
     * Ensures svc_freeargs is always called, preventing memory leaks on error paths.
     */
    template <typename ArgType>
    class RpcArgGuard {
    private:
        SVCXPRT* transp_;
        xdrproc_t xdr_func_;
        ArgType args_{};
        bool valid_;

    public:
        RpcArgGuard(SVCXPRT* transp, xdrproc_t xdr_func)
            : transp_(transp), xdr_func_(xdr_func), valid_(false) {
            valid_ = svc_getargs(transp_, xdr_func_, reinterpret_cast<char*>(&args_));
        }

        ~RpcArgGuard() {
            if (valid_) {
                svc_freeargs(transp_, xdr_func_, reinterpret_cast<char*>(&args_));
            }
        }

        bool IsValid() const { return valid_; }
        const ArgType& GetArgs() const { return args_; }
    };
}

RpcDispatchResult DispatchDataPlaneRpc(struct svc_req* rqstp, SVCXPRT* transp,
                                       std::shared_ptr<IServiceHandler> handler,
                                       const PeerContext& peer) {
    if (!handler) {
        LogError("RPC Dispatcher failed: Service handler is null");
        svcerr_systemerr(transp);
        return RpcDispatchResult::FatalError;
    }

    switch (rqstp->rq_proc) {
        case HEREIS: {
            MutableProduct mprod{};
            if (tl_hereis_scratchpad.empty()) {
                tl_hereis_scratchpad.resize(MAX_RPC_BUF_NEEDED);
            }
            
            // Seed the scratchpad bounds to prevent dynamic allocation for standard products
            mprod.payload.buffer = tl_hereis_scratchpad.data();
            mprod.payload.capacity = tl_hereis_scratchpad.size();

            if (!svc_getargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_mutable_product), reinterpret_cast<char*>(&mprod))) {
                LogError("RPC decode error during HEREIS from %s", peer.hostname.c_str());
                svcerr_decode(transp);
                return RpcDispatchResult::Handled; 
            }

            Product prod;
            prod.info = mprod.info;
            prod.data = mprod.payload.buffer;

            int error = handler->OnHereIs(peer, prod);

            // XDR FREE block
            svc_freeargs(transp, reinterpret_cast<xdrproc_t>(xdr_net_mutable_product), reinterpret_cast<char*>(&mprod));

            // Return control to the caller rather than forcing an exit()
            if (error && error != static_cast<int>(PqStatus::Dup) &&
                error != static_cast<int>(PqStatus::Big) &&
                error != static_cast<int>(PqStatus::NotFound)) {
                LogError("HEREIS fatal error %d (sys/queue allocation) for product '%s'", 
                  error, prod.info.ident.c_str());
                svcerr_systemerr(transp);
                return RpcDispatchResult::FatalError;
            }
            
            return RpcDispatchResult::Handled;
        }

        case COMINGSOON: {
            RpcArgGuard<ComingSoonArgsNet> guard(transp, reinterpret_cast<xdrproc_t>(xdr_net_comingsoon_args));
            if (!guard.IsValid()) {
                LogError("RPC decode error during COMINGSOON from %s", peer.hostname.c_str());
                svcerr_decode(transp);
                return RpcDispatchResult::Handled;
            }

            const auto& args = guard.GetArgs();
            int error = handler->OnComingSoon(peer, args.info, args.pktsz);
            int replyCode = 0;

            if (error == 0) {
                replyCode = 0;
            } else if (error == static_cast<int>(PqStatus::Dup) ||
                       error == static_cast<int>(PqStatus::Big) ||
                       error == static_cast<int>(PqStatus::NotFound)) {
                replyCode = 3; // Legacy standard code for DONT_SEND
            } else {
                LogError("COMINGSOON fatal error %d (e.g., ENOMEM/ENOSPC) for product '%s' (%u bytes)", 
                         error, args.info.ident.c_str(), args.pktsz);
                svcerr_systemerr(transp);
                return RpcDispatchResult::FatalError;
            }

            if (!svc_sendreply(transp, reinterpret_cast<xdrproc_t>(xdr_int), reinterpret_cast<char*>(&replyCode))) {
                LogError("COMINGSOON failed to send RPC reply for product '%s'", args.info.ident.c_str());
                svcerr_systemerr(transp);
            }
            
            return RpcDispatchResult::Handled;
        }

        case BLKDATA: {
            RpcArgGuard<DataPktNet> guard(transp, reinterpret_cast<xdrproc_t>(xdr_net_datapkt));
            if (!guard.IsValid()) {
                LogError("RPC decode error during BLKDATA from %s", peer.hostname.c_str());
                svcerr_decode(transp);
                return RpcDispatchResult::Handled;
            }

            const auto& dpkp = guard.GetArgs();
            int error = handler->OnBlkData(peer,
                reinterpret_cast<const uint8_t*>(dpkp.signaturep),
                dpkp.pktnum,
                reinterpret_cast<const uint8_t*>(dpkp.dbuf_val),
                dpkp.dbuf_len
            );

            if (error && error != static_cast<int>(PqStatus::Dup) &&
                error != static_cast<int>(PqStatus::Big) &&
                error != static_cast<int>(PqStatus::NotFound)) {
                LogError("BLKDATA fatal error %d for packet %u", error, dpkp.pktnum);
                svcerr_systemerr(transp);
                return RpcDispatchResult::FatalError;
            }
            
            return RpcDispatchResult::Handled;
        }

        case NOTIFICATION: {
            RpcArgGuard<ProdInfo> guard(transp, reinterpret_cast<xdrproc_t>(xdr_net_prod_info));
            if (!guard.IsValid()) {
                LogError("RPC decode error during NOTIFICATION from %s", peer.hostname.c_str());
                svcerr_decode(transp);
                return RpcDispatchResult::Handled;
            }
            
            handler->OnNotification(peer, guard.GetArgs());
            return RpcDispatchResult::Handled;
        }

        default:
            // Control plane command (FEEDME, NOTIFYME, HIYA, etc.)
            // The caller is responsible for evaluating these.
            return RpcDispatchResult::NotHandled;
    }
}

} // namespace rdm
