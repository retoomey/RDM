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
    std::string accumulated_line;
    int start_line = 0;
    int line_number = 0;
    int success_count = 0;
    bool has_error = false; // Track all errors instead of aborting instantly

    // Lambda to process a fully buffered rule
    auto process_rule = [&]() -> void {
        if (accumulated_line.empty()) return;

        std::string full_line = accumulated_line;
        int current_rule_line = start_line;
        accumulated_line.clear();
        start_line = 0;

        // Strip leading whitespace
        full_line.erase(0, full_line.find_first_not_of(" \t"));
        
        // Tolerate indented comments
        if (full_line.empty() || full_line[0] == '#') return;

        // Extract exactly 3 fields (FeedType, Pattern, Action)
        size_t start_idx = 0;
        std::vector<std::string> tokens;
        for (int i = 0; i < 3 && start_idx != std::string::npos; ++i) {
            size_t end_idx = full_line.find_first_of(" \t", start_idx);
            if (end_idx != std::string::npos) {
                tokens.push_back(full_line.substr(start_idx, end_idx - start_idx));
                start_idx = full_line.find_first_not_of(" \t", end_idx);
            } else {
                tokens.push_back(full_line.substr(start_idx));
                start_idx = std::string::npos;
            }
        }

        if (tokens.size() < 3) {
            LogError("Syntax error at line {}, missing FeedType, Pattern, or Action", current_rule_line);
            has_error = true;
            return;
        }

        FeedType ft;
        if (FeedType::Parse(tokens[0].c_str(), ft) != FEEDTYPE_OK) {
            LogError("Feedtype error at line {}: \"{}\"", current_rule_line, tokens[0]);
            has_error = true;
            return;
        }

        auto act = ActionFactory::Create(tokens[2], ctx);
        if (!act) {
            LogError("Unknown action \"{}\" at line {}", tokens[2], current_rule_line);
            has_error = true;
            return;
        }

        // Capture the remaining line exactly as-is for the arguments
        std::string args;
        if (start_idx != std::string::npos) {
            args = full_line.substr(start_idx);
        }

        try {
            auto entry = std::make_unique<PqactEntry>(ft, tokens[1], std::move(act), args);
            config.entries.push_back(std::move(entry));
            success_count++;
        } catch (const std::regex_error& e) {
            LogError("Regex compilation error at line {}: \"{}\" - {}", current_rule_line, tokens[1], e.what());
            has_error = true;
            return;
        }
    };

    bool prev_ended_with_slash = false;

    while (std::getline(file, line)) {
        line_number++;
        
        // Strip Windows CRLF carriage returns
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Check for legacy backslash continuation
        bool ends_with_slash = false;
        if (!line.empty() && line.back() == '\\') {
            line.pop_back();
            ends_with_slash = true;
        }

        // Strictly speaking, column-1 hash is a comment. Flush buffer.
        if (!line.empty() && line[0] == '#') {
            process_rule();
            continue;
        }

        // Blank lines. Flush buffer.
        if (line.find_first_not_of(" \t") == std::string::npos) {
            process_rule();
            continue;
        }

        // Any line starting with whitespace is a native LDM continuation
        bool starts_with_ws = !line.empty() && (line[0] == ' ' || line[0] == '\t');

        if (accumulated_line.empty()) {
            accumulated_line = line;
            start_line = line_number;
        } else {
            if (prev_ended_with_slash || starts_with_ws) {
                // Append continuation to current rule
                accumulated_line += line;
            } else {
                // New entry detected in column 1 -> process previous rule
                process_rule();
                accumulated_line = line;
                start_line = line_number;
            }
        }
        prev_ended_with_slash = ends_with_slash;
    }

    // Process the final accumulated rule
    process_rule();

    if (has_error) {
        LogWarning("Configuration-file \"{}\" parsed with errors.", filepath);
        return false;
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
