#include "config.h"
#include "QueueApp.h"
#include "Log.h"
#include "Timestamp.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

using namespace rdm;

class PqMonApp : public QueueApp {
private:
    int interval_{0};
    bool extended_{false};
    bool printSizePar_{false};
    bool list_extents_{false};

protected:
    void ConfigureOptions() override {
        QueueApp::ConfigureOptions();
        
        RegisterOption('i', "interval", "Poll queue after 'interval' secs (default 0)", "0");
        RegisterFlag('e', "Extended output format");
        RegisterFlag('S', "Print raw queue metrics for machine parsing");
    }

    bool ProcessOptions() override {
        if (!QueueApp::ProcessOptions()) return false;

        if (IsSet('i')) {
            interval_ = std::stoi(GetOption('i'));
        }
        extended_ = IsSet('e');
        printSizePar_ = IsSet('S');
        
        // In pqmon, the debug flag (-x) also triggers the free-extents dump
        list_extents_ = IsSet('x'); 

        return true;
    }

    bool Initialize() override {
        // Redirect stdout if an output file is provided via trailing arguments
        if (!positionalArgs_.empty()) {
            const char* outputfname = positionalArgs_[0].c_str();
            if (std::freopen(outputfname, "a+b", stdout) == nullptr) {
                LogError("Couldn't open \"{}\": {}", outputfname, std::strerror(errno));
                return false;
            }
        }

        if (!QueueApp::Initialize()) return false;

        if (!printSizePar_) {
            LogNotice("Starting Up ({})", getpgrp());
        }

        return true;
    }

    int Run() override {
        // Print the header if we aren't in machine-readable mode
        if (!printSizePar_) {
            if (extended_) {
                LogNotice("nprods nfree  nempty      nbytes  maxprods  maxfree  minempty    maxext    age    maxbytes");
            } else {
                LogNotice("nprods nfree  nempty      nbytes  maxprods  maxfree  minempty    maxext  age");
            }
        }

        while (!SignalManager::IsDone()){
            size_t nprods, nfree, nempty, nbytes, maxprods, maxfree, minempty, maxbytes, maxextent;
            double age_oldest;
            
            int status = pq_->getStats(nprods, nfree, nempty, nbytes,
                                       maxprods, maxfree, minempty, maxbytes,
                                       age_oldest, maxextent);
            if (status) {
                LogError("pq_stats() failed: {} (errno = {})", pq_->strerror(status), status);
                return EXIT_FAILURE;
            }

            if (printSizePar_) {
                double age_youngest;
                long minReside;
                off_t mvrtSize;
                size_t mvrtSlots;
                
                int isFull = pq_->isFull() ? 1 : 0;
                if (0 == nprods) {
                    age_youngest = -1;
                    minReside = -1;
                    mvrtSize = -1;
                    mvrtSlots = 0;
                } else {
                    Timestamp cleanMostRecent;
                    Timestamp cleanMinResTime;
                    
                    if ((status = pq_->getMostRecent(cleanMostRecent))) {
                        LogError("pq_getMostRecent() failed: {} (errno = {})", pq_->strerror(status), status);
                        return EXIT_FAILURE;
                    }
                    
                    Timestamp now = Timestamp::Now();
                    age_youngest = (now - cleanMostRecent).AsSeconds();
                    
                    if ((status = pq_->getMinVirtResTimeMetrics(cleanMinResTime, mvrtSize, mvrtSlots))) {
                        LogError("pq_getMinVirtResTimeMetrics() failed: {} (errno = {})", pq_->strerror(status), status);
                        return EXIT_FAILURE;
                    }
                    minReside = static_cast<long>(cleanMinResTime.tv_sec);
                }
                
                // Machine-readable format output
                fmt::print("{} {} {} {} {} {} {} {:.0f} {:.0f} {} {} {}\n", 
                    isFull,
                    static_cast<unsigned long>(pq_->getDataSize()), static_cast<unsigned long>(maxbytes),
                    static_cast<unsigned long>(nbytes),
                    static_cast<unsigned long>(pq_->getSlotCount()), static_cast<unsigned long>(maxprods),
                    static_cast<unsigned long>(nprods), age_oldest, age_youngest, minReside,
                    static_cast<long>(mvrtSize), static_cast<unsigned long>(mvrtSlots));
                
            } else {
                if (extended_) {
                    LogNotice("{:>6} {:>5} {:>7} {:>11} {:>9} {:>8} {:>9} {:>9} {:>9.0f} {:>11}",
                       nprods, nfree, nempty, nbytes, maxprods, maxfree, minempty, maxextent, age_oldest, maxbytes);
                } else {
                    LogNotice("{:>6} {:>5} {:>7} {:>11} {:>9} {:>8} {:>9} {:>9} {:>9.0f}",
                      nprods, nfree, nempty, nbytes, maxprods, maxfree, minempty, maxextent, age_oldest);
                }
                
                if (list_extents_) {
                    status = pq_->dumpFreeExtents();
                    if (status) {
                        LogError("pq_dumpFreeExtents failed: {} (errno = {})", pq_->strerror(status), status);
                        return EXIT_FAILURE;
                    }
                }
            }

            if (interval_ == 0) break;
            
            struct timeval tv;
            tv.tv_sec = interval_;
            tv.tv_usec = 0;
            select(0, nullptr, nullptr, nullptr, &tv);
        }

        return EXIT_SUCCESS;
    }

public:
    // Initialize the queue in ReadOnly mode
    PqMonApp() : QueueApp(PqFlags::ReadOnly, "Monitors the performance and capacity of the LDM product queue.") {}
};

int main(int argc, char *argv[]) {
    PqMonApp app;
    return app.Execute(argc, argv);
}
