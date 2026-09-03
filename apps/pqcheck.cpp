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
        // Open passively unless we explicitly need write access to clear the counter
        pqOpenFlags_ = force_ ? PqFlags::Default : PqFlags::ReadOnly;
        return true;
    }

    bool Initialize() override {
        SignalManager::Ignore(SIGALRM);
        SignalManager::Ignore(SIGCHLD);

        // Run base CLI argument parsing and logging setup
        if (!Application::Initialize()) return false;

        // Replicate QueueApp::Initialize() but with custom error trapping
        registry::setQueuePath(queuePath_);
        auto serializer = NetworkFactory::CreateSerializer();
        pq_ = StorageFactory::Create(serializer);
        
        int status = pq_->open(queuePath_, pqOpenFlags_);
        if (status) {
            // Map both explicit Corrupt status and EINVAL (bad magic/version) to exit code 4
            if (status == static_cast<int>(PqStatus::Corrupt) || status == EINVAL) {
                LogError("The product-queue \"{}\" is inconsistent or corrupt", queuePath_);
                LogShutdown();
                exit(4);
            } else {
                LogError("pq_open failed: {}: {}", queuePath_, pq_->strerror(status));
                LogShutdown();
                exit(1);
            }
        }
        lastDataVersion_ = pq_->getDataVersion();

        LogNotice("Starting Up ({})", getpgrp());
        return true;
    }

    int Run() override {
        int status = 0;
        size_t write_count = 0;

        if (force_) {
            status = pq_->clearWriteCount();
            if (status) {
                LogError("clearWriteCount() failure: {}: {}", queuePath_, pq_->strerror(status));
                return (status == static_cast<int>(PqStatus::Corrupt) || status == EINVAL) ? 4 : 1;
            }
        } else {
            status = pq_->getWriteCount(write_count);
            if (status) {
                if (ENOSYS == status) {
                    LogError("Product-queue \"{}\" doesn't have a writer-counter", queuePath_);
                    return 2;
                } else {
                    LogError("getWriteCount() failure: {}: {}", queuePath_, pq_->strerror(status));
                    return (status == static_cast<int>(PqStatus::Corrupt) || status == EINVAL) ? 4 : 1;
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
