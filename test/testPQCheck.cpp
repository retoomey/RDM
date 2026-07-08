#include "config.h"
#include "Log.h"
#include "Registry.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "NetworkFactory.h"
#include "PrivilegeManager.h"
#include "SignalManager.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <libgen.h>
#include <unistd.h>

using namespace rdm;

static const char* TEST_QUEUE = "/tmp/pqcheck_test_fixture.pq";

static int setup(void) {
    std::remove(TEST_QUEUE);
    return 0;
}

static int teardown(void) {
    std::remove(TEST_QUEUE);
    return 0;
}

static void test_writer_counter_lifecycle(void) {
    auto serializer = NetworkFactory::CreateSerializer();
    auto pq = StorageFactory::Create(serializer);
    
    size_t count = 999;
    
    // 1. Create the queue
    int status = pq->create(TEST_QUEUE, 0666, PqFlags::Default, 0, 500000, 100);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    pq->close(); // A clean close drops the writer count back to 0

    // 2. Open ReadOnly (Should NOT increment the writer count)
    status = pq->open(TEST_QUEUE, PqFlags::ReadOnly);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    
    status = pq->getWriteCount(count);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(count, 0);
    pq->close();

    // 3. Open for Writing (Should increment the writer count)
    status = pq->open(TEST_QUEUE, PqFlags::Default);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    
    status = pq->getWriteCount(count);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(count, 1);

    // 4. Force clear the writer count while it is open
    status = pq->clearWriteCount();
    CU_ASSERT_EQUAL(status, 0);

    status = pq->getWriteCount(count);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(count, 0);
    
    pq->close();
}

int main(int argc, char** argv) {
    int exitCode = EXIT_FAILURE;

    if (LogInitialize(argv[0])) {
        std::fprintf(stderr, "Couldn't open logging system\n");
        return exitCode;
    }

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* suite = CU_add_suite("PQCheck Dynamic OOP Logic Suite", setup, teardown);
        
        if (suite != nullptr) {
            if (CU_ADD_TEST(suite, test_writer_counter_lifecycle)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                CU_basic_run_tests();
                exitCode = CU_get_number_of_tests_failed();
            }
        }
        CU_cleanup_registry();
    }

    LogShutdown();
    return exitCode;
}
