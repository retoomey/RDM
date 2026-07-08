#include "config.h"
#include "ULDB.h"
#include "Log.h"
#include "Registry.h"
#include "ProdClass.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace rdm;

static const std::string TEST_DB_PATH = "/tmp/test_uldb_fixture.db";

// [NEW] Local test instance replaces global Uldb::Instance() access calls
static Uldb testUldb;

static ProdClass get_clss_all() {
    ProdClass clss;
    clss.from_sec = 0;
    clss.from_usec = 0;
    clss.to_sec = 0x7fffffff;
    clss.to_usec = 999999;
    ProdSpec spec;
    spec.feedtype = rdm::ANY;
    spec.pattern = ".*";
    clss.specs.push_back(spec);
    return clss;
}

static struct sockaddr_storage new_sock_addr() {
    static uint32_t ip_counter = 0x0A000001;
    struct sockaddr_storage ss{};
    struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(&ss);
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(ip_counter++);
    sin->sin_port = htons(388);
    return ss;
}

static int setup(void) {
    registry::setDirectory("/tmp");
    registry::reset();
    
    // Clean up any stray shm and backing blocks
    testUldb.Delete(TEST_DB_PATH);
    
    if (testUldb.Create(TEST_DB_PATH, 1024 * 1024) != UldbStatus::SUCCESS) {
        return -1;
    }
    // [NEW] Open must be explicitly invoked to map memory pools after initialization
    if (testUldb.Open(TEST_DB_PATH) != UldbStatus::SUCCESS) {
        return -1;
    }
    return 0;
}

static int teardown(void) {
    testUldb.Close();
    testUldb.Delete(TEST_DB_PATH);
    registry::reset();
    return 0;
}

static void test_invalid_args(void) {
    struct sockaddr_storage ss{};
    ProdClass allowed;
    UldbStatus stat = testUldb.AddProcess(-1, 6, &ss, get_clss_all(), allowed, 0, 1);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::ARG));
    stat = testUldb.Remove(-1);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::ARG));
}

static void test_add_get_remove(void) {
    struct sockaddr_storage ss = new_sock_addr();
    ProdClass desired = get_clss_all();
    ProdClass allowed;
    pid_t mock_pid = 99999;
    
    UldbStatus stat = testUldb.AddProcess(mock_pid, 6, &ss, desired, allowed, 0, 1);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::SUCCESS));
    
    unsigned size = 0;
    stat = testUldb.GetSize(size);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::SUCCESS));
    CU_ASSERT_EQUAL(size, 1);
    
    std::vector<UldbEntry> entries;
    stat = testUldb.GetEntries(entries);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::SUCCESS));
    CU_ASSERT_EQUAL(entries.size(), 1);
    
    if (!entries.empty()) {
        CU_ASSERT_EQUAL(entries[0].pid, mock_pid);
        CU_ASSERT_EQUAL(entries[0].protoVers, 6);
        CU_ASSERT_EQUAL(entries[0].isNotifier, 0);
        CU_ASSERT_EQUAL(entries[0].isPrimary, 1);
        CU_ASSERT_EQUAL(entries[0].sockAddr.ss_family, AF_INET);
    }
    
    stat = testUldb.Remove(mock_pid);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::SUCCESS));
    
    testUldb.GetSize(size);
    CU_ASSERT_EQUAL(size, 0);
}

static void test_anti_dos_termination(void) {
    registry::putBool(registry::RegistryKey::AntiDosEnabled, true);
    struct sockaddr_storage ss = new_sock_addr();
    ProdClass allowed;
    
    pid_t child_pid = fork();
    if (child_pid == 0) {
        while(true) pause();
    }
    
    UldbStatus stat = testUldb.AddProcess(child_pid, 6, &ss, get_clss_all(), allowed, 0, 1);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::SUCCESS));
    
    usleep(50000);
    
    pid_t new_connection_pid = child_pid + 1;
    stat = testUldb.AddProcess(new_connection_pid, 6, &ss, get_clss_all(), allowed, 0, 1);
    CU_ASSERT_EQUAL(static_cast<int>(stat), static_cast<int>(UldbStatus::SUCCESS));
    
    int wstatus;
    pid_t res = waitpid(child_pid, &wstatus, 0);
    CU_ASSERT_EQUAL(res, child_pid);
    CU_ASSERT_TRUE(WIFSIGNALED(wstatus));
    CU_ASSERT_EQUAL(WTERMSIG(wstatus), SIGTERM);
    
    testUldb.Remove(new_connection_pid);
    testUldb.Remove(child_pid);
}

static void test_concurrent_access(void) {
    const int NUM_CHILDREN = 10;
    pid_t children[NUM_CHILDREN];
    
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        children[i] = fork();
        if (children[i] == 0) {
            pid_t my_pid = getpid();
            struct sockaddr_storage ss = new_sock_addr();
            reinterpret_cast<struct sockaddr_in*>(&ss)->sin_addr.s_addr = htonl(my_pid);
            ProdClass allowed;
            
            // [NEW] Child processes initialize their own clean connection to the shared segment
            Uldb childUldb;
            if (childUldb.Open(TEST_DB_PATH) != UldbStatus::SUCCESS) {
                _exit(1);
            }
            
            if (childUldb.AddProcess(my_pid, 6, &ss, get_clss_all(), allowed, 0, 1) != UldbStatus::SUCCESS) {
                _exit(1);
            }
            
            usleep(10000);
            
            if (childUldb.Remove(my_pid) != UldbStatus::SUCCESS) {
                _exit(2);
            }
            
            childUldb.Close();
            _exit(0);
        }
    }
    
    for (int i = 0; i < NUM_CHILDREN; ++i) {
        int wstatus;
        waitpid(children[i], &wstatus, 0);
        CU_ASSERT_TRUE(WIFEXITED(wstatus));
        CU_ASSERT_EQUAL(WEXITSTATUS(wstatus), 0);
    }
    
    unsigned size = 0;
    testUldb.GetSize(size);
    CU_ASSERT_EQUAL(size, 0);
}

int main(const int argc, const char* const * argv) {
    int exitCode = EXIT_FAILURE;
    if (LogInitialize(argv[0])) {
        std::fprintf(stderr, "Couldn't initialize logging system\n");
        return exitCode;
    }
    log_set_level(LOG_LEVEL_FATAL);
    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite("Modernized ULDB Test Suite", setup, teardown);
        if (testSuite != nullptr) {
            CU_ADD_TEST(testSuite, test_invalid_args);
            CU_ADD_TEST(testSuite, test_add_get_remove);
            CU_ADD_TEST(testSuite, test_anti_dos_termination);
            CU_ADD_TEST(testSuite, test_concurrent_access);
            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return exitCode;
}
