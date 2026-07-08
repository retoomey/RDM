#include "NetworkUtils.h"
#include "Log.h"
#include "Registry.h"
#include "Timestamp.h"
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>

namespace rdm {
namespace network {

bool IpAddressesAreEqual(const struct sockaddr_storage* addr1, const struct sockaddr_storage* addr2) {
    if (addr1->ss_family != addr2->ss_family) return false;
    if (addr1->ss_family == AF_INET) {
        auto a1 = reinterpret_cast<const struct sockaddr_in*>(addr1);
        auto a2 = reinterpret_cast<const struct sockaddr_in*>(addr2);
        return std::memcmp(&a1->sin_addr, &a2->sin_addr, sizeof(a1->sin_addr)) == 0;
    } else if (addr1->ss_family == AF_INET6) {
        auto a1 = reinterpret_cast<const struct sockaddr_in6*>(addr1);
        auto a2 = reinterpret_cast<const struct sockaddr_in6*>(addr2);
        return std::memcmp(&a1->sin6_addr, &a2->sin6_addr, sizeof(a1->sin6_addr)) == 0;
    }
    return false;
}

std::string GetLocalHostName() {
    std::string regName = registry::getString(registry::RegistryKey::Hostname);
    if (!regName.empty()) {
        return regName;
    }
    
    char hostBuffer[256];
    if (gethostname(hostBuffer, sizeof(hostBuffer)) == 0) {
        hostBuffer[sizeof(hostBuffer) - 1] = '\0';
        return std::string(hostBuffer);
    }
    
    LogError("Registry hostname empty and gethostname failed. Defaulting to 'localhost'.");
    return "localhost";
}

std::string GetHostByAddr(const struct sockaddr_storage* addr, socklen_t addrLen) {
    if (!addr) return "unknown";

    Timestamp start = Timestamp::Now();
    char hostname[NI_MAXHOST];
    char ipStr[NI_MAXHOST];

    // Try to get numeric first as a fallback identifier
    if (getnameinfo(reinterpret_cast<const struct sockaddr*>(addr), addrLen, ipStr, sizeof(ipStr), nullptr, 0, NI_NUMERICHOST) != 0) {
        std::strncpy(ipStr, "unknown_ip", sizeof(ipStr));
    }

    // Now try to resolve the actual name
    int status = getnameinfo(reinterpret_cast<const struct sockaddr*>(addr), addrLen, hostname, sizeof(hostname), nullptr, 0, 0);
    
    Timestamp stop = Timestamp::Now();
    double elapsed = (stop - start).AsSeconds();

    if (status != 0) {
        const char* reason = (status == EAI_NONAME) ? "address doesn't resolve to a name" :
                             (status == EAI_AGAIN)  ? "couldn't resolve name at this time" :
                             (status == EAI_FAIL)   ? "Unrecoverable error" :
                             (status == EAI_FAMILY) ? "invalid address family" :
                             (status == EAI_MEMORY) ? "out-of-memory" :
                             (status == EAI_OVERFLOW) ? "hostname buffer is too small" :
                             (status == EAI_SYSTEM) ? std::strerror(errno) : "unanticipated error";

        if (elapsed >= 10.0) { // RESOLVER_TIME_THRESHOLD
            LogWarning("Couldn't resolve \"{}\" to a hostname in {:.3f} seconds: {}", ipStr, elapsed, reason);
        } else {
            LogInfo("Couldn't resolve \"{}\" to a hostname in {:.3f} seconds: {}", ipStr, elapsed, reason);
        }
        return std::string(ipStr);
    }

    if (elapsed >= 10.0) {
        LogWarning("Resolving {} to {} took {:.3f} seconds", ipStr, hostname, elapsed);
    } else {
        LogInfo("Resolving {} to {} took {:.3f} seconds", ipStr, hostname, elapsed);
    }

    return std::string(hostname);
}

std::string GetHostByAddr(const struct sockaddr_in* addr) {
    if (!addr) return "unknown";
    struct sockaddr_storage ss{};
    std::memcpy(&ss, addr, sizeof(struct sockaddr_in));
    return GetHostByAddr(&ss, sizeof(struct sockaddr_in));
}

bool IsLocalHost(const std::string& remoteHost) {
    if (remoteHost == "localhost" || remoteHost == "loopback" || 
        remoteHost == "127.0.0.1" || remoteHost == "::1") {
        return true;
    }

    std::string me = GetLocalHostName();
    if (me == remoteHost) return true;
    
    // Basic canonical name check using getaddrinfo to see if it resolves to a loopback
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(remoteHost.c_str(), nullptr, &hints, &res) == 0 && res) {
        if (res->ai_canonname && me == res->ai_canonname) {
            freeaddrinfo(res);
            return true;
        }
        freeaddrinfo(res);
    }
    
    return false;
}

} // namespace network
}
