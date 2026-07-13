#pragma once

#include "Application.h"
#include "IServer.h"
#include "ULDB.h"
#include "AclManager.h"
#include "ProcessManager.h"
#include <memory>
#include <string>

namespace rdm {

class RdmEngine : public Application {
private:
    std::string ldmBindAddr_;
    unsigned ldmPort_{388};
    unsigned maxClients_{256};
    bool becomeDaemon_{false}; // Flipped behavior: foreground by default
    bool checkOnly_{false};
    bool disableNagles_{false};
    unsigned int maxHereis_{16384};
    std::unique_ptr<IServer> server_;
    Uldb uldb_;
    std::unique_ptr<AclManager> aclManager_;
    ProcessManager processManager_;

    int Daemonize();
    pid_t Reap(pid_t pid, int options);

protected:
    void ConfigureOptions() override;
    bool ProcessOptions() override;
    bool Initialize() override;
    int Run() override;

public:
    RdmEngine();
    ~RdmEngine() override;
    
    // Programmatic entry point for embedding supervisors
    int StartEngine(int argc, char* argv[]);
    void StopEngine();
};

} // namespace rdm
