/**
 * @file uldbutil.cpp
 * @brief Modernized utility for accessing the upstream database.
 * @author Robert Toomey
 * @date May 2026
 */
#include "config.h"
#include "Application.h"
#include "Log.h"
#include "NetworkUtils.h"
#include "ULDB.h"
#include <iostream>
#include <vector>
#include <cstdlib>

using namespace rdm;

class UldbUtilApp : public Application {
private:
    bool deleteDb_{false};
    Uldb uldb_; // [NEW] Local explicit instance instantiation replacing Singleton tracking

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterFlag('d', "Delete the Upstream LDM Database");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;
        if (!positionalArgs_.empty()) {
            LogError("Too many arguments provided.");
            return false;
        }
        deleteDb_ = IsSet('d');
        return true;
    }

    int Run() override {
        if (deleteDb_) {
            // [NEW] Use explicit local object instance calls
            UldbStatus stat = uldb_.Delete("");
            int status = static_cast<int>(stat);
            if (status) {
                if (UldbStatus::EXIST == stat) {
                    fmt::print(stderr, "The upstream LDM database doesn't exist\n");
                    return 2;
                } else {
                    LogError("Couldn't delete the upstream LDM database");
                    return 3;
                }
            } else {
                fmt::print(stdout, "Upstream LDM database deleted successfully.\n");
            }
        } else {
            // [NEW] Open active database mapping from instance stack context
            UldbStatus stat = uldb_.Open("");
            int status = static_cast<int>(stat);
            if (status) {
                if (UldbStatus::EXIST == stat) {
                    fmt::print(stderr, "The upstream LDM database doesn't exist. Is the LDM running?\n");
                    return 2;
                } else {
                    LogError("Couldn't open the upstream LDM database");
                    return 3;
                }
            } else {
                std::vector<UldbEntry> entries;
                stat = uldb_.GetEntries(entries);
                status = static_cast<int>(stat);
                if (status) {
                    LogError("Couldn't get database entries");
                    uldb_.Close();
                    return 3;
                } else {
                    for (const auto& entry : entries) {
                        const char* const type = entry.isNotifier ? "notifier" : "feeder";
                        fmt::print(stdout, "{} {} {} {} {} {}\n",
                                entry.pid,
                                entry.protoVers,
                                type,
                                network::GetHostByAddr(&entry.sockAddr, sizeof(struct sockaddr_storage)),
                                entry.prodClass.ToString(),
                                entry.isPrimary ? "primary" : "alternate");
                    }
                }
                (void) uldb_.Close();
            }
        }
        return 0;
    }

public:
    UldbUtilApp() : Application("Prints or deletes the Upstream LDM Database (ULDB).") {}
};

int main(int argc, char* argv[]) {
    UldbUtilApp app;
    return app.Execute(argc, argv);
}
