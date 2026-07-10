#include "SunRpcServer.h"
#include "SunRpcClient.h"
#include "SunRpcSerializer.h"
#include "ServiceAddr.h"

using namespace rdm;

extern "C" {

IServer* rdm_create_server() {
    return new SunRpcServer();
}

IClient* rdm_create_client(const char* host, uint16_t port, unsigned int timeout_sec) {
    ServiceAddr target(host, port);
    return new SunRpcClient(std::move(target), timeout_sec);
}

IClient* rdm_create_client_handoff(
    const char* host, uint16_t port, int existing_socket, 
    const struct sockaddr_storage* remote_addr, unsigned int timeout_sec) {
    ServiceAddr target(host, port);
    return new SunRpcClient(std::move(target), existing_socket, remote_addr, timeout_sec);
}

IProductSerializer* rdm_create_serializer() {
    return new SunRpcSerializer();
}

} // extern "C"
