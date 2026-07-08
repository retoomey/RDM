/**
 * Write data-products from the LDM product-queue to the standard output stream.
 *
 * Modernized C++ Port
 */
#include "config.h"
#include "QueueApp.h"
#include "Log.h"
#include "Pattern.h"
#include "Timestamp.h"
#include "Signature.h"
#include "SignalManager.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <unistd.h>

using namespace rdm;

static volatile sig_atomic_t g_stats_req = 0;

static void sigusr1_handler(int) {
    g_stats_req = 1;
}

class PqCatApp : public QueueApp {
private:
    bool md5_check_{false};
    bool showProdOrigin_{false};
    bool queueSanityCheck_{false};
    int interval_{0};
    ProdClass clss_;
    size_t nprods_{0};
    
    // NEW: The app owns its own iteration state!
    std::unique_ptr<IQueueCursor> cursor_; 

    static int WriteProd(const ProdInfo& info, const void* datap, void* xprod, size_t size, void* arg) {
        auto* app = static_cast<PqCatApp*>(arg);
        if (log_is_enabled_info) {
            if (app->showProdOrigin_) {
                LogInfo("{} {}", info.ToString(log_is_enabled_debug), info.origin);
            } else {
                LogInfo("{}", info.ToString(log_is_enabled_debug));
            }
        }
        if (app->md5_check_) {
           Signature check = Signature::GenerateMD5(datap, info.sz);
           if (info.signature != check) {
               LogError("signature mismatch: {} != {}", info.signature.ToString(), check.ToString());
           }
        }
        if (write(STDOUT_FILENO, datap, info.sz) != static_cast<ssize_t>(info.sz)) {
            int errnum = errno;
            LogSyserr("data write failed");
            return errnum;
        }
        app->nprods_++;
        return 0;
    }

    static int TallyProds(const ProdInfo& info, const void* datap, void* xprod, size_t size, void* arg) {
        auto* app = static_cast<PqCatApp*>(arg);
        app->nprods_++;
        return 0;
    }

    void DumpStats() {
        LogNotice("Number of products {}", nprods_);
    }

protected:
    void ConfigureOptions() override {
        QueueApp::ConfigureOptions();
        RegisterOption('i', "interval", "Poll queue after 'interval' secs (default 0)", "0");
        RegisterFlag('e', "Extended output format");
        RegisterFlag('S', "Print raw queue metrics for machine parsing");
        RegisterFlag('c', "Check, verify MD5 signature");
        RegisterFlag('O', "Include product origin in verbose output (requires -v)");
        RegisterFlag('s', "Check queue for sanity/non-corruption");
        RegisterOption('p', "pattern", "Interested in products matching 'pattern' (default '.*')", ".*");
        RegisterOption('f', "feedtype", "Scan for data of type 'feedtype' (default 'ANY')", "ANY");
        RegisterOption('o', "offset", "Set the 'from' time 'offset' secs before now", "");
    }

    bool ProcessOptions() override {
        if (!QueueApp::ProcessOptions()) return false;
        md5_check_ = IsSet('c');
        showProdOrigin_ = IsSet('O');
        queueSanityCheck_ = IsSet('s');
        if (IsSet('i')) {
            interval_ = std::stoi(GetOption('i'));
        }
        Timestamp now = Timestamp::Now();
        clss_.from_sec = Timestamp::ZERO.tv_sec;
        clss_.from_usec = Timestamp::ZERO.tv_usec;
        clss_.to_sec = Timestamp::ENDT.tv_sec;
        clss_.to_usec = Timestamp::ENDT.tv_usec;
        if (IsSet('o') && !GetOption('o').empty()) {
            now.tv_sec -= std::stoi(GetOption('o'));
            clss_.from_sec = now.tv_sec;
            clss_.from_usec = now.tv_usec;
        }
        ProdSpec spec;
        spec.feedtype = ANY;
        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), spec.feedtype) != FEEDTYPE_OK) {
                LogError("Bad feedtype \"{}\"", GetOption('f'));
                return false;
            }
        }
        clss_.specs.push_back({spec.feedtype, GetOption('p')});

        return true;
    }

    bool Initialize() override {
        if (!positionalArgs_.empty()) {
            const char* outputfname = positionalArgs_[0].c_str();
            if (std::freopen(outputfname, "a+b", stdout) == nullptr) {
                LogError("Couldn't open \"{}\": {}", outputfname, std::strerror(errno));
                return false;
            }
        }
        if (!QueueApp::Initialize()) return false;

        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sa.sa_handler = sigusr1_handler;
        sigaction(SIGUSR1, &sa, nullptr);

        LogNotice("Starting Up ({}): prod_class={}", getpgrp(), clss_.ToString());
        
        cursor_ = pq_->CreateCursor();
        cursor_->setCursor(Timestamp(clss_.from_sec, static_cast<int32_t>(clss_.from_usec)));
        return true;
    }

    int Run() override {
        int status = 0;
        while (!SignalManager::IsDone()){
            if (g_stats_req) {
                DumpStats();
                g_stats_req = 0;
            }
            
            status = queueSanityCheck_
                ? cursor_->sequence(Match::GreaterThan, clss_, TallyProds, this)
                : cursor_->sequence(Match::GreaterThan, clss_, WriteProd, this);
                
            if (status == 0) continue;
            if (status == static_cast<int>(PqStatus::End)) {
                LogDebug("End of Queue");
            } else if (status == EAGAIN || status == EACCES) {
                LogDebug("Hit a lock");
            } else {
                LogError("pq_sequence failed: {} (errno = {})", pq_->strerror(status), status);
                return EXIT_FAILURE;
            }
            if (interval_ == 0) break;
            struct timeval tv = { static_cast<time_t>(interval_), 0 };
            select(0, nullptr, nullptr, nullptr, &tv);
        }

        if (queueSanityCheck_) {
            size_t nprods_stat, nfree, nempty, nbytes, maxprods, maxfree, minempty, maxbytes, maxextent;
            double age_oldest;
            status = pq_->getStats(nprods_stat, nfree, nempty, nbytes,
                                   maxprods, maxfree, minempty, maxbytes,
                                   age_oldest, maxextent);
            if (status) {
                LogError("pq_stats failed: {} (errno = {})", pq_->strerror(status), status);
                return EXIT_FAILURE;
            }
            if (nprods_ == nprods_stat) {
                LogNotice("pqcat queueSanityCheck: Number of products tallied consistent with value in queue");
                return EXIT_SUCCESS;
            } else {
                LogError("pqcat queueSanityCheck: Product count doesn't match");
                LogError("products tallied: {}   Value in queue: {}", nprods_, nprods_stat);
                return EXIT_FAILURE;
            }
        }
        DumpStats();
        return EXIT_SUCCESS;
    }

public:
    PqCatApp() : QueueApp(PqFlags::ReadOnly, "Extracts products from the LDM product queue and writes them to standard output.") {}
};

int main(int argc, char *argv[]) {
    PqCatApp app;
    return app.Execute(argc, argv);
}
