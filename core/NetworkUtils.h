#pragma once

#include <string>

#include <sys/socket.h>
#include <netinet/in.h>

namespace rdm {
namespace network {

bool
IpAddressesAreEqual(const struct sockaddr_storage* addr1, const struct sockaddr_storage* addr2);

/**
 * @brief Gets the hostname of the local machine.
 * Checks the registry first, falling back to OS gethostname().
 * Replaces legacy `ghostname()`.
 */
std::string
GetLocalHostName();

/**
 * @brief Resolves a sockaddr_storage to a human-readable hostname or IP string.
 * Safely wraps getnameinfo() for both IPv4 and IPv6.
 * Replaces legacy `hostbyaddr()`.
 * * @param addr Pointer to the sockaddr_storage structure.
 * @param addrLen Length of the address structure.
 * @return std::string containing the hostname or fallback IP string.
 */
std::string
GetHostByAddr(const struct sockaddr_storage * addr, socklen_t addrLen);

/**
 * @brief Legacy overload for IPv4 sockaddr_in (used by uldbutil).
 * @param addr Pointer to the IPv4 sockaddr_in structure.
 * @return std::string containing the hostname or fallback IP string.
 */
std::string
GetHostByAddr(const struct sockaddr_in * addr);

/**
 * @brief Checks if the provided host string represents the local machine.
 * Replaces legacy `isMe()`.
 * * @param remoteHost The hostname or IP to verify.
 * @return true if the host is local, false otherwise.
 */
bool
IsLocalHost(const std::string& remoteHost);

inline std::string
AppendUpstreamHostToOrigin(const std::string& originalOrigin, const char * hostId)
{
  std::string newOrigin    = originalOrigin;
  const std::string sepStr = "_v_";
  auto pos = newOrigin.find(sepStr);

  if (pos != std::string::npos) {
    newOrigin.erase(pos);
  }

  newOrigin += sepStr;
  if (hostId) {
    newOrigin += hostId;
  }
  return newOrigin;
}

} // namespace network
}
