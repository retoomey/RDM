/**
 * Accesses the LDM registry.
 *
 * Modernized C++ Port
 */
#include "config.h"
#include "Application.h"
#include "Log.h"
#include "Registry.h"
#include "Signature.h"
#include "Timestamp.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace rdm;

enum RegStatus {
    COMMAND_SUCCESS = 0,
    COMMAND_SYNTAX = 1,
    NO_SUCH_ENTRY = 2,
    SYSTEM_ERROR = 3
};

class RegUtilApp : public Application {
private:
    enum Action {
        UNKNOWN, CREATE, PRINT, PUT_BOOL, PUT_STRING,
        PUT_UINT, PUT_SIGNATURE, PUT_TIME, RESET, REMOVE
    } action_{UNKNOWN};

    std::string stringVal_;
    std::string signatureVal_;
    std::string timestampVal_;
    unsigned long uintVal_{0};
    std::string booleanVal_;
    bool quiet_{false};

    RegStatus PrintPath(const std::string& path) const {
        LogDebug("{} printing path \"{}\"", quiet_ ? "Quietly" : "Non-quietly", path);
        auto val = registry::getString(path);
        if (val) {
            fmt::print(stdout, "{}\n", *val);
            return COMMAND_SUCCESS;
        }

        auto values = registry::getAllValues(path);
        if (values.empty()) {
            if (!quiet_) LogError("No such value or node: \"{}\"", path);
            return NO_SUCH_ENTRY;
        }

        for (const auto& [nodePath, nodeValue] : values) {
            fmt::print(stdout, "{} : {}\n", nodePath, nodeValue);
        }
        return COMMAND_SUCCESS;
    }

    RegStatus DeletePath(const std::string& path) const {
        LogDebug("{} deleting path \"{}\"", quiet_ ? "Quietly" : "Non-quietly", path);
        if (!registry::deleteValue(path)) {
            if (!quiet_) LogError("No such value or node: \"{}\"", path);
            return NO_SUCH_ENTRY;
        }
        registry::flush();
        return COMMAND_SUCCESS;
    }

    RegStatus ActUponPathList(RegStatus (RegUtilApp::*func)(const std::string&) const) const {
        RegStatus status = COMMAND_SUCCESS;
        for (const auto& path : positionalArgs_) {
            RegStatus stat = (this->*func)(path);
            if (stat > status) status = stat;
            if (status >= SYSTEM_ERROR) break;
        }
        return status;
    }

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        
        RegisterFlag('c', "Create Registry");
        RegisterFlag('R', "Reset Registry");
        RegisterFlag('r', "Remove Parameter(s)");
        RegisterFlag('q', "Be quiet about missing values or nodes");
        
        RegisterOption('d', "dir", "Path name of registry directory (default: system config)");
        RegisterOption('b', "bool", "Boolean registry value: TRUE, FALSE");
        RegisterOption('h', "sig", "Data-product signature as 32 hexadecimal characters");
        RegisterOption('s', "string", "String registry value");
        RegisterOption('t', "time", "Time registry value as YYYYMMDDThhmmss[.uuuuuu]");
        RegisterOption('u', "uint", "Unsigned integer registry value");
    }

    bool SetAction(Action newAction) {
        if (action_ != UNKNOWN && action_ != newAction) {
            LogError("Cannot mix multiple action operations together.");
            return false;
        }
        action_ = newAction;
        return true;
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;

        quiet_ = IsSet('q');

        if (IsSet('d') && !GetOption('d').empty()) {
            registry::setDirectory(GetOption('d'));
        }

        if (IsSet('c')) {
            if (!SetAction(CREATE)) return false;
        }
        
        if (IsSet('R')) {
            if (!SetAction(RESET)) return false;
        }
        
        if (IsSet('r')) {
            if (!SetAction(REMOVE)) return false;
        }

        if (IsSet('b')) {
            std::string bVal = GetOption('b');
            if (strcasecmp(bVal.c_str(), "TRUE") == 0 || bVal == "1") booleanVal_ = "TRUE";
            else if (strcasecmp(bVal.c_str(), "FALSE") == 0 || bVal == "0") booleanVal_ = "FALSE";
            else {
                LogError("Not a boolean value: \"{}\"", bVal);
                return false;
            }
            if (!SetAction(PUT_BOOL)) return false;
        }

        if (IsSet('h')) {
            auto sigOpt = Signature::Parse(GetOption('h'));
            if (!sigOpt || GetOption('h').length() != 32) {
                LogError("Not a signature: \"{}\"", GetOption('h'));
                return false;
            }
            signatureVal_ = sigOpt->ToString();
            if (!SetAction(PUT_SIGNATURE)) return false;
        }

        if (IsSet('s')) {
            stringVal_ = GetOption('s');
            if (!SetAction(PUT_STRING)) return false;
        }

        if (IsSet('t')) {
            auto optTs = Timestamp::Parse(GetOption('t'));
            if (!optTs.has_value()) {
                LogError("Not a valid timestamp format (YYYYMMDDThhmmss.uuuuuu): \"{}\"", GetOption('t'));
                return false;
            }
            timestampVal_ = optTs->ToString();
            if (!SetAction(PUT_TIME)) return false;
        }

        if (IsSet('u')) {
            std::string uVal = GetOption('u');
            char* end;
            errno = 0;
            uintVal_ = std::strtoul(uVal.c_str(), &end, 0);
            if (*end != 0 || (uintVal_ == 0 && errno != 0)) {
                LogError("Not an unsigned integer: \"{}\"", uVal);
                return false;
            }
            if (!SetAction(PUT_UINT)) return false;
        }

        if (action_ == UNKNOWN) {
            action_ = PRINT;
        }

        return true;
    }

    int Run() override {
        RegStatus status = COMMAND_SUCCESS;

        switch (action_) {
            case CREATE:
                if (!positionalArgs_.empty()) {
                    LogError("Create action takes no positional arguments");
                    return COMMAND_SYNTAX;
                }
                registry::flush();
                break;

            case RESET:
                if (!positionalArgs_.empty()) {
                    LogError("Reset action takes no positional arguments");
                    return COMMAND_SYNTAX;
                }
                registry::reset();
                break;

            case REMOVE:
                if (positionalArgs_.empty()) {
                    LogError("Removal action requires absolute pathname(s)");
                    return COMMAND_SYNTAX;
                }
                LogDebug("Removing registry node(s)");
                status = ActUponPathList(&RegUtilApp::DeletePath);
                break;

            case PRINT:
                LogDebug("Printing registry");
                if (positionalArgs_.empty()) {
                    status = PrintPath("/");
                } else {
                    status = ActUponPathList(&RegUtilApp::PrintPath);
                }
                break;

            default:
                // Write/Put operations
                if (positionalArgs_.empty()) {
                    LogError("Put action requires a value pathname");
                    return COMMAND_SYNTAX;
                }
                
                bool putSuccess = false;
                std::string targetPath = positionalArgs_[0]; // Put applies to the first positional arg

                switch (action_) {
                    case PUT_BOOL:      putSuccess = registry::putString(targetPath, booleanVal_); break;
                    case PUT_UINT:      putSuccess = registry::putString(targetPath, std::to_string(uintVal_)); break;
                    case PUT_STRING:    putSuccess = registry::putString(targetPath, stringVal_); break;
                    case PUT_TIME:      putSuccess = registry::putString(targetPath, timestampVal_); break;
                    case PUT_SIGNATURE: putSuccess = registry::putString(targetPath, signatureVal_); break;
                    default:            std::abort();
                }

                if (!putSuccess) {
                    status = SYSTEM_ERROR;
                } else {
                    registry::flush();
                }
                break;
        }

        return status;
    }

public:
    RegUtilApp() : Application("LDM registry configuration utility.") {}
};

int main(int argc, char* argv[]) {
    RegUtilApp app;
    return app.Execute(argc, argv);
}
