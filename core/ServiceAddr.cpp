#include "ServiceAddr.h"
#include "Log.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <arpa/inet.h>

namespace rdm {

ServiceAddr::ServiceAddr(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {}

std::optional<ServiceAddr> ServiceAddr::Parse(const std::string& spec, const std::string& defaultHost, uint16_t defaultPort) {
    if (spec.empty()) {
        return std::nullopt;
    }

    std::string host = defaultHost;
    uint16_t port = defaultPort;

    size_t firstColon = spec.find_first_of(':');
    size_t lastColon = spec.find_last_of(':');
    size_t closeBracket = spec.find_last_of(']');

    bool hasPortSeparator = false;

    if (lastColon != std::string::npos) {
        if (closeBracket != std::string::npos) {
            // Brackets exist. Port separator is a colon AFTER the close bracket.
            if (lastColon > closeBracket) hasPortSeparator = true;
        } else {
            // No brackets. If there's only ONE colon, it's a host:port.
            if (firstColon == lastColon) hasPortSeparator = true;
        }
    }

    if (hasPortSeparator) {
        try {
            unsigned long parsedPort = std::stoul(spec.substr(lastColon + 1));
            if (parsedPort > 0xFFFF) return std::nullopt;
            port = static_cast<uint16_t>(parsedPort);
        } catch (...) {
            LogError("Invalid port specified in service address: {}", spec);
            return std::nullopt; 
        }

        std::string hostPart = spec.substr(0, lastColon);
        if (!hostPart.empty()) {
            // Strip IPv6 brackets if present
            if (hostPart.front() == '[' && hostPart.back() == ']') {
                host = hostPart.substr(1, hostPart.length() - 2);
            } else {
                host = hostPart;
            }
        }
    } else {
        // No port separator. Is it strictly a port number, or a hostname/IP?
        bool isAllDigits = !spec.empty() && std::all_of(spec.begin(), spec.end(), [](unsigned char c) { return std::isdigit(c); });
        
        if (isAllDigits) {
            try {
                unsigned long parsedPort = std::stoul(spec);
                if (parsedPort > 0xFFFF) return std::nullopt;
                port = static_cast<uint16_t>(parsedPort);
            } catch (...) {
                return std::nullopt;
            }
        } else {
            // If it has brackets but no port, strip them for the internal host representation
            if (spec.front() == '[' && spec.back() == ']') {
                host = spec.substr(1, spec.length() - 2);
            } else {
                host = spec;
            }
        }
    }

    if (host.empty()) {
        LogError("No host resolved from service address specification: {}", spec);
        return std::nullopt;
    }

    return ServiceAddr(host, port);
}

std::string ServiceAddr::ToString() const {
    // Re-wrap IPv6 addresses in brackets for standard URL/URI formatting
    if (host_.find(':') != std::string::npos) {
        return "[" + host_ + "]:" + std::to_string(port_);
    }
    return host_ + ":" + std::to_string(port_);
}

bool ServiceAddr::Resolve(struct sockaddr_storage* outAddr, socklen_t* outLen, int family, bool serverSide) const {
    if (!outAddr || !outLen) {
        LogError("Invalid arguments passed to ServiceAddr::Resolve");
        return false;
    }

    struct addrinfo hints{};
    hints.ai_family = family;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    //hints.ai_flags = serverSide ? (AI_NUMERICSERV | AI_PASSIVE) : (AI_NUMERICSERV | AI_ADDRCONFIG);
    hints.ai_flags = serverSide ? (AI_NUMERICSERV | AI_PASSIVE) : AI_NUMERICSERV;

    std::string portStr = std::to_string(port_);
    struct addrinfo* addrInfo = nullptr;

    int status = getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &addrInfo);
    if (status != 0) {
        LogError("Couldn't resolve address {} - {}", ToString(), gai_strerror(status));
        return false;
    }

    if (addrInfo) {
        *outLen = addrInfo->ai_addrlen;
        std::memcpy(outAddr, addrInfo->ai_addr, *outLen);
        freeaddrinfo(addrInfo);
        return true;
    }

    return false;
}

bool ServiceAddr::operator==(const ServiceAddr& other) const {
    return port_ == other.port_ && host_ == other.host_;
}

bool ServiceAddr::operator<(const ServiceAddr& other) const {
    if (host_ == other.host_) {
        return port_ < other.port_;
    }
    return host_ < other.host_;
}

}
