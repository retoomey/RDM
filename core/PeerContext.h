#pragma once
#include <string>

#include <sys/socket.h>

namespace rdm {
struct PeerContext {
  struct sockaddr_storage addr;
  socklen_t               addrLen;
  std::string             ip_string;
  std::string             hostname;
  unsigned int            send_size;
  unsigned int            recv_size;
};
}
