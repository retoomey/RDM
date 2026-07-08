#include "ConfParser.h"
#include "Log.h"
#include "ServiceAddr.h"

#include <fstream>
#include <cctype>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <filesystem>

#include <libgen.h>

namespace rdm {

ServerConfig ConfParser::Parse(const std::string& filepath, unsigned int defaultPort) {
    ServerConfig config;
    if (!ParseRecursive(filepath, config, 0, defaultPort)) {
        LogError("Failed to parse root configuration file: {}", filepath);
    }
    return config;
}

std::vector<std::string> ConfParser::Tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    bool escape = false;

    for (char c : line) {
        if (escape) {
            current += c;
            escape = false;
        } else if (c == '\\') {
            if (in_quotes) {
                escape = true;
            } else {
                current += c;
            }
        } else if (c == '"') {
            in_quotes = !in_quotes;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else if (c == '#' && !in_quotes) {
            break; // Ignore comments
        } else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

bool ConfParser::ParseRecursive(const std::string& filepath, ServerConfig& config, int depth, unsigned int defaultPort) {
    if (depth > 10) {
        LogError("Maximum nested INCLUDE depth exceeded at: {}", filepath);
        return false;
    }

    std::ifstream file(filepath);
    if (!file) {
        LogError("Couldn't open LDM configuration-file \"{}\"", filepath);
        return false;
    }

    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        lineNum++;
        auto tokens = Tokenize(line);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];
        for (auto& c : cmd) c = std::toupper(static_cast<unsigned char>(c));

        if (cmd == "ALLOW") {
            if (tokens.size() < 3 || tokens.size() > 5) {
                LogError("ALLOW rule requires 3 to 5 arguments. ({}:{})", filepath, lineNum);
                continue;
            }
            FeedType ft;
            if (FeedType::Parse(tokens[1], ft) != FEEDTYPE_OK) continue;
            
            std::string hostPattern = tokens[2];
            std::string okPattern = (tokens.size() >= 4) ? tokens[3] : ".*";
            std::string notPattern = (tokens.size() == 5) ? tokens[4] : "";

            try {
                config.allowRules.emplace_back(ft, hostPattern, okPattern, notPattern);
            } catch (const std::exception& e) {
                LogError("Failed to compile regex for ALLOW rule ({}:{}): {}", filepath, lineNum, e.what());
            }
        }
        else if (cmd == "ACCEPT") {
            if (tokens.size() != 4) {
                LogError("ACCEPT rule requires 4 arguments. ({}:{})", filepath, lineNum);
                continue;
            }
            FeedType ft;
            if (FeedType::Parse(tokens[1], ft) != FEEDTYPE_OK) continue;

            try {
                config.acceptRules.emplace_back(ft, tokens[2], tokens[3], true);
            } catch (const std::exception& e) {
                LogError("Failed to compile regex for ACCEPT rule ({}:{}): {}", filepath, lineNum, e.what());
            }
        }
        else if (cmd == "EXEC") {
            if (tokens.size() != 2) {
                LogError("EXEC rule requires exactly 2 arguments. ({}:{})", filepath.c_str(), lineNum);
                continue;
            }
            try {
                Wordexp words(tokens[1]);
                ExecRule rule;
                rule.command = std::move(words);
                config.execRules.push_back(std::move(rule));
            } catch (const std::invalid_argument& e) {
                LogError("Couldn't decode EXEC command. ({}:{}): {}", filepath, lineNum, e.what());
            }
        }
        else if (cmd == "REQUEST") {
            if (tokens.size() != 4) {
                LogError("REQUEST rule requires 4 arguments. ({}:{})", filepath, lineNum);
                continue;
            }
            FeedType ft;
            if (FeedType::Parse(tokens[1], ft) != FEEDTYPE_OK) continue;

            std::string hostSpec = tokens[3];
            auto sa = ServiceAddr::Parse(hostSpec, "", defaultPort);
            if (!sa) {
                LogError("Invalid host/port specified in REQUEST rule ({}:{})", filepath, lineNum);
                continue;
            }

            RequestRule req;
            req.feedtype = ft;
            req.pattern = tokens[2];
            req.upstreamHost = sa->GetHost();
            req.port = sa->GetPort();
            config.requestRules.push_back(req);
        }
        else if (cmd == "INCLUDE") {
            if (tokens.size() != 2) {
                LogError("INCLUDE rule requires 2 arguments. ({}:{})", filepath, lineNum);
                continue;
            }
            std::string includePath = tokens[1];
            if (!includePath.empty() && includePath[0] != '/') {
                if (!includePath.empty() && includePath[0] != '/') {
                  std::filesystem::path baseDir = std::filesystem::path(filepath).parent_path();
                  includePath = (baseDir / includePath).string();
                }
            }
            ParseRecursive(includePath, config, depth + 1, defaultPort);
        }
        else if (cmd == "RECEIVE" || cmd == "MULTICAST") {
            LogWarning("Command '{}' unsupported without WANT_MULTICAST ({} {})", cmd, filepath, lineNum);
        }
        else {
            LogWarning("Unknown command '{}' ignored. ({}:{})", cmd, filepath, lineNum);
        }
    }

    return true;
}

}
