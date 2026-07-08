#include "PqactParser.h"
#include "ActionFactory.h"
#include "Pattern.h"
#include "Log.h"
#include <fstream>
#include <vector>

namespace rdm {
namespace pqact {

bool PqactParser::Parse(const std::string& filepath, PqactContext& ctx, PqactConfig& config) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LogError("Couldn't open configuration-file \"{}\"", filepath);
        return false;
    }
    config.Clear();
    std::string line;
    int line_number = 0;
    int success_count = 0;
    while (std::getline(file, line)) {
        line_number++;
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> tokens;
        size_t start = 0, end;
        while ((end = line.find('\t', start)) != std::string::npos) {
            if (end != start) {
                tokens.push_back(line.substr(start, end - start));
            }
            start = end + 1;
        }
        if (start < line.length()) {
            tokens.push_back(line.substr(start));
        }
        if (tokens.size() < 3) {
            LogError("Syntax error at line {}, not enough tab-delimited fields", line_number);
            return false;
        }
        FeedType ft;
        if (FeedType::Parse(tokens[0].c_str(), ft) != FEEDTYPE_OK) {
            LogError("Feedtype error at line {}: \"{}\"", line_number, tokens[0]);
            return false;
        }
        auto act = ActionFactory::Create(tokens[2], ctx);
        if (!act) {
            LogError("Unknown action \"{}\" at line {}", tokens[2], line_number);
            return false;
        }
        std::string args;
        for (size_t i = 3; i < tokens.size(); ++i) {
            if (i > 3) args += "\t";
            args += tokens[i];
        }
        try {
            auto entry = std::make_unique<PqactEntry>(ft, tokens[1], std::move(act), args);
            config.entries.push_back(std::move(entry));
            success_count++;
        } catch (const std::regex_error& e) {
            LogError("Regex compilation error at line {}: \"{}\" - {}", line_number, tokens[1], e.what());
            return false;
        }
    }
    if (success_count > 0) {
        LogInfo("Successfully read configuration-file \"{}\" ({} rules)", filepath, success_count);
        return true;
    }
    LogWarning("Configuration-file \"{}\" contains no valid entries.", filepath);
    return true;
}

}
}
