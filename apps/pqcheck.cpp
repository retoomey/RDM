/**
 * @file pqcheck.cpp
 * @brief Modernized utility to vet an LDM product-queue.
 * @author Robert Toomey
 * @date May 2026
 */
#include "QueueApp.h"

using namespace rdm;

class PqCheckApp : public QueueApp {
private:
    bool force_{false};

protected:
    void ConfigureOptions() override {
        QueueApp::ConfigureOptions();
        
        // Register pqcheck's specific flag
        RegisterFlag('F', "Force. Set the writer-counter to zero (creating it if necessary).");
    }

    bool ProcessOptions() override {
        if (!QueueApp::ProcessOptions()) return false;
        
        force_ = IsSet('F');
        return true;
    }

    bool Initialize() override {
        // Ignore signals specific to this app
        SignalManager::Ignore(SIGALRM);
        SignalManager::Ignore(SIGCHLD);
        
        if (!QueueApp::Initialize()) return false;
        
        LogNotice("Starting Up (%d)", getpgrp());
        return true;
    }

    int Run() override {
        int status = 0;
        size_t write_count = 0;

        if (force_) {
            status = pq_->clearWriteCount();
            if (status) {
                LogError("clearWriteCount() failure: {}: {}", queuePath_, pq_->strerror(status));
                return (status == static_cast<int>(PqStatus::Corrupt)) ? 4 : 1;
            }
        } else {
            status = pq_->getWriteCount(write_count);
            if (status) {
                if (ENOSYS == status) {
                    LogError("Product-queue \"{}\" doesn't have a writer-counter", queuePath_);
                    return 2;
                } else {
                    LogError("getWriteCount() failure: {}: {}", queuePath_, pq_->strerror(status));
                    return (status == static_cast<int>(PqStatus::Corrupt)) ? 4 : 1;
                }
            }
        }

        LogInfo("The writer-counter of the product-queue is {}", write_count);
        return write_count == 0 ? 0 : 3;
    }

public:
    PqCheckApp() : QueueApp(0, "Checks the writer-counter status of the LDM product queue.") {}
};

int main(int argc, char *argv[]) {
    PqCheckApp app;
    return app.Execute(argc, argv);
}
