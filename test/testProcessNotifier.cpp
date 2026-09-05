#include "config.h"
#include "ProcessNotifier.h"
#include "Log.h"
#include "SignalManager.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>

using namespace rdm;

static SharedSyncState* g_state = nullptr;

static int setup() {
    // Map anonymous shared memory to simulate the queue's control region
    g_state = static_cast<SharedSyncState*>(mmap(nullptr, sizeof(SharedSyncState),
                                                 PROT_READ | PROT_WRITE,
                                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (g_state == MAP_FAILED) return -1;
    return ProcessNotifier::Initialize(g_state);
}

static int teardown() {
    if (g_state != MAP_FAILED) munmap(g_state, sizeof(SharedSyncState));
    return 0;
}

static void test_readonly_polling_fallback() {
    ProcessNotifier reader(g_state, true); // Force read-only mode
    uint64_t startVersion = reader.GetVersion();

    pid_t pid = fork();
    if (pid == 0) {
        usleep(200000); // Sleep 200ms
        ProcessNotifier writer(g_state, false);
        writer.Notify();
        exit(0);
    }

    // Reader should fall back to polling, catch the update, and exit without timing out
    int status = reader.Wait(startVersion, 2);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(reader.GetVersion() > startVersion);
    
    waitpid(pid, nullptr, 0);
}

static void test_robust_mutex_recovery() {
    ProcessNotifier writer(g_state, false);
    uint64_t startVersion = writer.GetVersion();

    pid_t pid = fork();
    if (pid == 0) {
        // Child maliciously locks the mutex and instantly dies to simulate a crash
        pthread_mutex_lock(&g_state->mutex);
        exit(99); 
    }

    int wstatus;
    waitpid(pid, &wstatus, 0);
    CU_ASSERT_TRUE(WIFEXITED(wstatus));

    // Parent attempts to notify. It should encounter EOWNERDEAD, 
    // run pthread_mutex_consistent, and successfully complete the broadcast.
    int status = writer.Notify();
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(writer.GetVersion() > startVersion);
}

int main(int argc, char** argv) {
    if (LogInitialize(argv[0])) return EXIT_FAILURE;
    log_set_level(LOG_LEVEL_FATAL); // Suppress expected EOWNERDEAD error logs

    if (CUE_SUCCESS == CU_initialize_registry()) {
        SignalManager::Initialize();
        CU_Suite* suite = CU_add_suite("ProcessNotifier IPC Suite", setup, teardown);
        if (suite) {
            CU_ADD_TEST(suite, test_readonly_polling_fallback);
            CU_ADD_TEST(suite, test_robust_mutex_recovery);
            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return CU_get_number_of_tests_failed();
}
