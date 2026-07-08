#pragma once
#include <rpc/rpc.h>
#include <memory>
#include "IServiceHandler.h"
#include "PeerContext.h"

namespace rdm {

/**
 * @brief Indicates the execution status of an RPC dispatch attempt.
 */
enum class RpcDispatchResult {
    NotHandled, ///< The requested procedure was not a data-plane command.
    Handled,    ///< The command was processed successfully (or safely ignored, e.g., duplicates).
    FatalError  ///< A severe system or storage error occurred; the caller must drop the connection.
};

/**
 * @brief Isolates and handles heavy data-plane RPC requests (HEREIS, COMINGSOON, BLKDATA, NOTIFICATION).
 * * This engine serves as the single source of truth for memory management, bounds-checking, 
 * and decoding of incoming LDM product streams, preventing DRY violations between the 
 * upstream server and downstream client.
 * * @param rqstp   Pointer to the active RPC service request.
 * @param transp  Pointer to the RPC transport handle.
 * @param handler The active service handler for product insertion.
 * @param peer    Network context of the connected peer.
 * @return RpcDispatchResult status directing the caller's connection lifecycle.
 */
RpcDispatchResult DispatchDataPlaneRpc(struct svc_req* rqstp, SVCXPRT* transp,
                                       std::shared_ptr<IServiceHandler> handler,
                                       const PeerContext& peer);

} // namespace rdm
