#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <sys/socket.h>
#include <netdb.h>

namespace rdm {
class ServiceAddr {
private:
  std::string host_;
  uint16_t port_;

public:

  /**
   * @brief Constructs a new ServiceAddr.
   * @param host The hostname or IP address.
   * @param port The port number.
   */
  ServiceAddr(std::string host, uint16_t port);

  /**
   * @brief Parses a network address string (e.g., "host:port", "[ipv6]:port", "host", or "port").
   * @param spec The string specification to parse.
   * @param defaultHost Host to use if the spec only contains a port.
   * @param defaultPort Port to use if the spec only contains a host.
   * @return std::optional containing the ServiceAddr if successful, std::nullopt otherwise.
   */
  static std::optional<ServiceAddr>
  Parse(const std::string& spec,
    const std::string    & defaultHost = "",
    uint16_t             defaultPort = 0);

  /**
   * @brief Formats the endpoint as a string. Properly brackets IPv6 addresses.
   * @return Formatted string (e.g., "192.168.1.1:388" or "[2001:db8::1]:388").
   */
  std::string
  ToString() const;

  /**
   * @brief Resolves the host and port into a low-level sockaddr_storage structure.
   * Wraps getaddrinfo() to support both IPv4 and IPv6 transparently.
   * @param outAddr Pointer to the struct to populate.
   * @param outLen Pointer to the variable to store the address length.
   * @param family AF_UNSPEC (default), AF_INET, or AF_INET6.
   * @param serverSide True if binding a server socket (AI_PASSIVE), False for connecting clients.
   * @return True if resolution succeeded, false otherwise.
   */
  bool
  Resolve(struct sockaddr_storage * outAddr,
    socklen_t *                     outLen,
    int                             family     = AF_UNSPEC,
    bool                            serverSide = false) const;

  // Getters
  const std::string&
  GetHost() const { return host_; }

  uint16_t
  GetPort() const { return port_; }

  // Comparison operators for sorting/maps
  bool
  operator == (const ServiceAddr& other) const;
  bool
  operator < (const ServiceAddr& other) const;
};
}
