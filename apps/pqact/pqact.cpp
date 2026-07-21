/**
 * Toomey May 2026
 *
 * @file pqact.cpp
 * @brief Downstream Action/File-Dispatching Engine (Main Daemon)
 *
 * ==============================================================================
 * C++ Modernization & Refactoring Notes (2026)
 * ==============================================================================
 * This module was refactored from legacy C to modern C++17 to modernize the 
 * main entry point, centralizing process management and signal handling.
 *
 * Key Architectural Improvements:
 * 1. Path/String Management: Replaced highly vulnerable `getcwd()` and 
 * `strncat()` buffer manipulations with robust `std::string` concatenations. 
 * This dynamically binds the configuration file paths safely without relying on 
 * manual null-terminator math.
 *
 * 2. Signal & Cast Safety: Hardened `sigaction` implementations and applied 
 * strict C++ casting (`static_cast`) to prevent narrowing conversions during 
 * command-line argument parsing (`getopt`).
 * ==============================================================================
 */
#include "config.h"
#include "QueueApp.h"
#include <fcntl.h>
#include <libgen.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string>

#include "ProcessManager.h"
#include "SignalManager.h"
#include "Log.h"
#include "ProcessUtil.h"
#include "Pattern.h"
#include "PqactContext.h"
#include "PqactParser.h"
#include "StateFile.h"
#include "Timestamp.h"

using namespace rdm;

static Timestamp palt_last_insertion = Timestamp::NONE;

class PqActApp : public QueueApp {
private:
    int interval_{15};
    int toffset_{-1};
    int pipe_timeo_{60};
    ProdClass clss_;
    std::string activeDataDir_;
    std::string activeConfigPath_;
    bool hupped_{false};
    ProcessManager processManager_; 

    static int ConfigureStdio() {
        int status = os::openOnDevNullIfClosed(STDIN_FILENO, O_RDONLY);
        if (status == 0) {
            status = os::openOnDevNullIfClosed(STDOUT_FILENO, O_WRONLY);
            if (status == 0) {
                status = os::openOnDevNullIfClosed(STDERR_FILENO, O_RDWR);
            }
        }
        return status;
    }

    static void WarnIfOldest(const queue_par_t* queue_par, const prod_par_t* prod_par, const char* prefix) {
        if (queue_par->is_full && queue_par->early_cursor) {
            if (log_is_enabled_warning) {
                Timestamp now = Timestamp::Now();
                LogWarning("{} oldest product in full queue: age={} s, prod={}", 
                    prefix,
                    (now - queue_par->inserted).AsSeconds(),
                    prod_par->info.ToString(log_is_enabled_debug));
                LogWarning("Products might be deleted before being acted upon! "
                           "Queue too small? System overloaded?");
            }
        }
    }

    static void ProcessProduct(const prod_par_t* prod_par, const queue_par_t* queue_par, void* opt_arg) {
        auto* ctx = static_cast<pqact::PqactContext*>(opt_arg);
        if (!ctx) return;

        Product prod;
        prod.info = prod_par->info;
        prod.data = static_cast<const uint8_t*>(prod_par->data);

        if (log_is_enabled_info){
            LogInfo("{}", prod.info.ToString(log_is_enabled_debug));
        }

        bool didMatch = false;
        bool errorOccurred = false;

        ctx->config.ProcessProduct(prod, *ctx, prod_par->encoded, prod_par->size, didMatch, errorOccurred);

        if (didMatch) {
            // --- UPDATED CALL SITE ---
            WarnIfOldest(queue_par, prod_par, "Processed");
        }

        if (!errorOccurred) {
            palt_last_insertion = queue_par->inserted;
        }
    }

    static void ProcessDummyProduct(const char* ident, pqact::PqactContext* ctx) {
        prod_par_t prod_par;
        prod_par.data = nullptr;
        prod_par.encoded = nullptr;
        prod_par.size = 0;
        prod_par.info.arrival.tv_sec = -1;
        prod_par.info.arrival.tv_usec = -1;
        prod_par.info.origin = "localhost";
        prod_par.info.feedtype = ANY;
        prod_par.info.ident = ident;
        prod_par.info.signature.fill(0);

        queue_par_t queue_par{};
        queue_par.inserted.tv_sec = -1;
        queue_par.inserted.tv_usec = -1;

        ProcessProduct(&prod_par, &queue_par, ctx);
    }

protected:
    void ConfigureOptions() override {
        QueueApp::ConfigureOptions();
        RegisterOption('d', "datadir", "cd(1) to \"datadir\"", "");
        RegisterOption('f', "feedtype", "Only process products from feed \"feedtype\"", "ANY");
        RegisterOption('p', "pattern", "Only process products matching \"pattern\"", ".*");
        RegisterOption('i', "interval", "Loop, polling every \"interval\" seconds", "15");
        RegisterOption('t', "timeo", "Set write timeout for PIPE subprocs to \"timeo\" secs", "60");
        RegisterOption('o', "offset", "Start with products arriving \"offset\" seconds before now", "");
    }

    bool ProcessOptions() override {
        if (!QueueApp::ProcessOptions()) return false;

        Timestamp now = Timestamp::Now();
        clss_.from_sec = now.tv_sec;
        clss_.from_usec = now.tv_usec;
        clss_.to_sec = 0x7fffffff;
        clss_.to_usec = 999999;
        
        ProdSpec spec;
        spec.feedtype = ANY;
        spec.pattern = ".*";

        if (IsSet('d')) registry::setPqactDataDirPath(GetOption('d'));
        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), spec.feedtype) != FEEDTYPE_OK) {
                LogError("Bad feedtype \"{}\"", GetOption('f'));
                return false;
            }
        }

        if (IsSet('o')) toffset_ = std::stoi(GetOption('o'));
        if (IsSet('i')) interval_ = std::stoi(GetOption('i'));
        if (IsSet('t')) pipe_timeo_ = std::stoi(GetOption('t'));
        if (IsSet('p')) spec.pattern = GetOption('p');

        if (!positionalArgs_.empty()) {
            registry::setPqactConfigPath(positionalArgs_[0]);
        }

        activeDataDir_ = registry::getPqactDataDirPath();
        activeConfigPath_ = registry::getPqactConfigPath();

        if (!activeConfigPath_.empty() && activeConfigPath_[0] != '/') {
            char buf[PATH_MAX];
            if (getcwd(buf, sizeof(buf)) == nullptr) {
                LogSyserr("Couldn't get current working directory");
                return false;
            }
            activeConfigPath_ = std::string(buf) + "/" + activeConfigPath_;
        }

        clss_.specs.push_back(spec);
        return true;
    }

    bool Initialize() override {
        if (ConfigureStdio()) {
            LogError("Couldn't configure standard I/O streams for execution of child processes");
            return false;
        }

        SignalManager::Ignore(SIGPIPE);
        SignalManager::Ignore(SIGXFSZ);

        if (!QueueApp::Initialize()) return false;

        SignalManager::SetHangupHook([this]() {
            this->hupped_ = true;
        });

        return true;
    }

    int Run() override {
        long maxOpen = sysconf(_SC_OPEN_MAX);
        if (maxOpen == -1) maxOpen = _POSIX_OPEN_MAX;
        unsigned maxFdCount = static_cast<unsigned>(maxOpen - 6);
        
        auto* concreteQueue = pq_.get();
        if (!concreteQueue) {
            LogError("Fatal: Underlying product store is not a ProductQueue!");
            return EXIT_FAILURE;
        }
        
        pqact::PqactContext ctx(concreteQueue, maxFdCount, processManager_);
        ctx.pipeTimeo = pipe_timeo_;
        
        if (!pqact::PqactParser::Parse(activeConfigPath_, ctx, ctx.config)) {
            return EXIT_FAILURE;
        } else if (ctx.config.entries.empty()) {
            LogNotice("Configuration-file \"{}\" has no entries. You should probably not start this program instead.", activeConfigPath_);
        }
        
        auto cursor = pq_->CreateCursor();
        if (toffset_ >= 0) {
            clss_.from_sec -= toffset_;
            cursor->setCursor(Timestamp(clss_.from_sec, static_cast<int32_t>(clss_.from_usec)));
        } else {
            bool startAtTailEnd = true;
            clss_.from_sec = 0;
            clss_.from_usec = 0;
            Timestamp readTs;
            if (!os::StateFile::Read(activeConfigPath_, readTs)) {
                LogWarning("Couldn't get insertion-time of last-processed data-product from previous session");
            } else {
                Timestamp insertTimeTs(readTs.tv_sec, readTs.tv_usec);
                Timestamp now_check = Timestamp::Now();
                if (now_check < insertTimeTs) {
                    LogWarning("Time of last-processed data-product from previous session is in the future");
                } else {
                    char buf[80];
                    time_t tsec = insertTimeTs.tv_sec;
                    (void)std::strftime(buf, sizeof(buf), "%Y-%m-%d %T", gmtime(&tsec));
                    LogNotice("Starting from insertion-time {}.{:06} UTC", buf, static_cast<long>(insertTimeTs.tv_usec));
                    cursor->setCursor(insertTimeTs);
                    startAtTailEnd = false;
                }
            }
            if (startAtTailEnd) {
                LogNotice("Starting at tail-end of product-queue");
                Timestamp dummy;
                (void)cursor->setCursorToLast(clss_, dummy);
            }
        }
        
        LogInfo("{}", clss_.ToString());
        
        if (!activeDataDir_.empty()) {
            if (chdir(activeDataDir_.c_str()) == -1) {
                LogSyserr("cannot chdir to {}", activeDataDir_);
                return 4;
            }
        }
        
        ProcessDummyProduct("_BEGIN_", &ctx);
        int exitCode = EXIT_SUCCESS;
        
        while (!SignalManager::IsDone()) {
            if (hupped_) {
                LogNotice("Rereading configuration file {}", activeConfigPath_);
                (void) pqact::PqactParser::Parse(activeConfigPath_, ctx, ctx.config);
                hupped_ = false;
            }
            
            int status = cursor->next(false, clss_, ProcessProduct, false, &ctx);
            
            if (status) {
                if (status == static_cast<int>(PqStatus::End)) {
                    LogDebug("End of Queue");
                    if (interval_ == 0) break;
                    ctx.fileCache->SyncAll(false);
                } else if (status == EAGAIN || status == EACCES || status == EINTR) {
                    LogDebug("Hit a lock or was interrupted by a signal");
                    ctx.fileCache->EvictLru(pqact::FL_NOTRANSIENT);
                } else if (status == EDEADLK) {
                    LogSyserr("Deadlock detected (EDEADLK)");
                    ctx.fileCache->EvictLru(pqact::FL_NOTRANSIENT);
                } else {
                    LogError("pq_next() failure: {} (errno = {})", pq_->strerror(status), status);
                    exitCode = EXIT_FAILURE;
                    break;
                }
                
                if (SignalManager::IsDone()) break;
                
                struct timeval tv;
                tv.tv_sec = interval_;
                tv.tv_usec = 0;
                select(0, nullptr, nullptr, nullptr, &tv);
            }
            
            if (SignalManager::IsDone()) break;
            
            while (processManager_.Reap(-1, WNOHANG) > 0);
        }
        
        if (SignalManager::IsDone()) {
            if (palt_last_insertion == Timestamp::NONE) {
                LogNotice("No product was processed");
            } else {
                Timestamp now_close = Timestamp::Now();
                LogNotice("Last product processed was inserted {} s ago",
                        (now_close - palt_last_insertion).AsSeconds());
                if (!os::StateFile::Write(activeConfigPath_, palt_last_insertion)) {
                    LogError("Couldn't save insertion-time of last processed data-product");
                }
            }
        }

        // ==========================================================
        // BULLETPROOF CLEANUP LOGIC
        // ==========================================================
        LogNotice("pqact shutting down: signaling EXEC and PIPE children...");
        
        // 1. Ignore SIGTERM so pqact isn't killed while cleaning up its own mess
        SignalManager::Ignore(SIGTERM); 
        
        // 2. Explicitly send SIGTERM to all detached children
        processManager_.KillAll(SIGTERM);

        // 3. Block and reap until the process manager is entirely empty
        while (processManager_.Count() > 0) {
            processManager_.Reap(-1, 0); 
        }
        
        LogNotice("pqact shutdown complete. All children reaped.");
        // ==========================================================

        ctx.fileCache->CloseAll();
        return exitCode;
    }

public:
    PqActApp() : QueueApp(PqFlags::ReadOnly, "Dispatches local product queue files to decoders and actions.") {}
};

int main(int argc, char *argv[]) {
    PqActApp app;
    return app.Execute(argc, argv);
}
