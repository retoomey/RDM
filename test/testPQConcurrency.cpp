#include "config.h"
#include "Log.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "NetworkFactory.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <vector>
#include <random>
#include <cstring>
#include <chrono>
#include <atomic>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

using namespace rdm;

static const char* TEST_QUEUE = "/tmp/concurrency_stress.pq";

struct SharedStats {
    std::atomic<int> corruptions{0};
    std::atomic<int> commits{0};
    std::atomic<int> rollbacks{0};
    std::atomic<int> reads{0};
};

static SharedStats* g_stats = nullptr;

static int setup(void) {
    std::remove(TEST_QUEUE);
    g_stats = static_cast<SharedStats*>(mmap(nullptr, sizeof(SharedStats),
                                             PROT_READ | PROT_WRITE,
                                             MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    new (g_stats) SharedStats();
    return 0;
}

static int teardown(void) {
    std::remove(TEST_QUEUE);
    if (g_stats) {
        g_stats->~SharedStats();
        munmap(g_stats, sizeof(SharedStats));
    }
    return 0;
}

static int reader_verify_callback(const ProdInfo& info, const void* datap, void* xprod, size_t size, void* arg) {
    Signature checkSig = Signature::GenerateMD5(datap, info.sz);
    if (checkSig != info.signature) {
        LogError("CORRUPTION DETECTED! Product {} has an invalid payload.", info.ident);
        g_stats->corruptions++;
    }
    g_stats->reads++;
    return 0;
}

static void writer_process(int process_id, int iterations) {
    auto pq = StorageFactory::Create(NetworkFactory::CreateSerializer());
    
    if (pq->open(TEST_QUEUE, PqFlags::Default) != 0) {
        LogError("Writer process {} failed to open queue", process_id);
        exit(1);
    }

    std::mt19937 rng(process_id ^ static_cast<unsigned int>(std::time(nullptr)));
    std::uniform_int_distribution<int> size_dist(1024, 65536);
    std::uniform_int_distribution<int> action_dist(1, 100);

    for (int i = 0; i < iterations; ++i) {
        ProdInfo info;
        info.feedtype = EXP;
        info.seqno = i;
        info.sz = size_dist(rng);
        info.origin = "proc_" + std::to_string(process_id);
        info.ident = "payload_" + std::to_string(process_id) + "_" + std::to_string(i);
        info.arrival = Timestamp::Now();
        std::vector<char> buffer(info.sz, static_cast<char>(process_id & 0xFF));
        info.signature = Signature::GenerateMD5(buffer.data(), info.sz);

        std::unique_ptr<IQueueEntry> entry; 
        int status = pq->newElement(info, entry);
        if (status == 0) {
            std::memcpy(entry->getPayloadPointer(), buffer.data(), info.sz);
            
            if (action_dist(rng) <= 15) {
                g_stats->rollbacks++;
            } else {
                if (entry->commit() == 0) {
                    g_stats->commits++;
                } else {
                    g_stats->rollbacks++;
                }
            }
        }
    }

    pq->close();
    exit(0);
}

static void reader_process(int process_id) {
    auto pq = StorageFactory::Create(NetworkFactory::CreateSerializer());
    
    if (pq->open(TEST_QUEUE, PqFlags::ReadOnly) != 0) {
        LogError("Reader process {} failed to open queue", process_id);
        exit(1);
    }

    ProdClass clss;
    clss.from_sec = 0; clss.from_usec = 0;
    clss.to_sec = 0x7fffffff; clss.to_usec = 999999;
    clss.specs.push_back({EXP, ".*"});

    auto cursor = pq->CreateCursor();
    cursor->setCursor(Timestamp::ZERO);

    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        int status = cursor->sequence(Match::GreaterThan, clss, reader_verify_callback, nullptr);
        if (status == -1) {
            usleep(5000);
        }
    }

    pq->close();
    exit(0);
}

static void test_extreme_concurrency(void) {
    auto pq_init = StorageFactory::Create(NetworkFactory::CreateSerializer());
    
    int status = pq_init->create(TEST_QUEUE, 0666, PqFlags::Default, 0, 2048000, 500);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    pq_init->close();

    const int NUM_WRITERS = 8;
    const int NUM_READERS = 16;
    const int ITERATIONS_PER_WRITER = 1000;

    std::vector<pid_t> children;
    LogNotice("Forking {} writers and {} readers...", NUM_WRITERS, NUM_READERS);

    for (int i = 0; i < NUM_WRITERS; ++i) {
        pid_t pid = fork();
        if (pid == 0) writer_process(i, ITERATIONS_PER_WRITER);
        else children.push_back(pid);
    }

    for (int i = 0; i < NUM_READERS; ++i) {
        pid_t pid = fork();
        if (pid == 0) reader_process(i);
        else children.push_back(pid);
    }

    for (pid_t pid : children) {
        int wstatus;
        waitpid(pid, &wstatus, 0);
    }

    LogNotice("=== MULTI-PROCESS STRESS TEST RESULTS ===");
    LogNotice("Successful Commits: {}", g_stats->commits.load());
    LogNotice("RAII Rollbacks (Intentional + Contention): {}", g_stats->rollbacks.load());
    LogNotice("Total Reads Validated: {}", g_stats->reads.load());
    LogNotice("Corrupted Products Detected: {}", g_stats->corruptions.load());

    CU_ASSERT_EQUAL(g_stats->corruptions.load(), 0);
    CU_ASSERT_TRUE(g_stats->commits.load() > 0);
    CU_ASSERT_TRUE(g_stats->reads.load() > 0);
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;

    if (LogInitialize(argv[0])) return exitCode;
    log_set_level(LOG_LEVEL_WARNING);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite("ProductQueue Multi-Process Concurrency Suite", setup, teardown);
        if (testSuite != nullptr) {
            CU_ADD_TEST(testSuite, test_extreme_concurrency);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }

    LogShutdown();
    return exitCode;
}
