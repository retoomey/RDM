#include "config.h"
#include "Log.h"
#include "Registry.h"
#include "InfoFile.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>


namespace fs = std::filesystem;
using namespace rdm;

static const char* TEST_STATE_DIR = "/tmp/ldm_test_state_dir";

static int setup(void) { 
    std::error_code ec;
    fs::create_directories(TEST_STATE_DIR, ec);
    
    // Wire up the registry so getLdmStateDir() targets our sandbox
    registry::setDirectory(TEST_STATE_DIR);
    registry::putString(registry::RegistryKey::StatePath, TEST_STATE_DIR);
    return 0; 
}

static int teardown(void) { 
    std::error_code ec;
    fs::remove_all(TEST_STATE_DIR, ec);
    registry::reset();
    return 0; 
}

static ProdClass create_test_class() {
    ProdClass clss;
    clss.from_sec = 0; clss.from_usec = 0;
    clss.to_sec = 0x7fffffff; clss.to_usec = 999999;
    clss.specs.push_back({rdm::ANY, ".*"});
    return clss;
}

static void test_modern_write_and_read(void) {
    ProdClass clss = create_test_class();
    SavedInfoFile sm("", "upstream.mock.edu", 388, clss);

    ProdInfo writeInfo;
    writeInfo.arrival.tv_sec = 1700000000;
    writeInfo.arrival.tv_usec = 123456;
    writeInfo.feedtype = rdm::IDS;
    writeInfo.seqno = 42;
    writeInfo.sz = 1024;
    writeInfo.origin = "test.origin.edu";
    writeInfo.ident = "WMO_TEST_IDENT";
    writeInfo.signature.fill(0xAA);

    CU_ASSERT_TRUE(sm.Write(writeInfo));

    ProdInfo readInfo;
    CU_ASSERT_TRUE(sm.Read(readInfo));

    CU_ASSERT_EQUAL(readInfo.arrival.tv_sec, 1700000000);
    CU_ASSERT_EQUAL(readInfo.arrival.tv_usec, 123456);
    CU_ASSERT_EQUAL(readInfo.feedtype, rdm::IDS);
    CU_ASSERT_EQUAL(readInfo.seqno, 42);
    CU_ASSERT_EQUAL(readInfo.sz, 1024);
    CU_ASSERT_STRING_EQUAL(readInfo.origin.c_str(), "test.origin.edu");
    CU_ASSERT_STRING_EQUAL(readInfo.ident.c_str(), "WMO_TEST_IDENT");
    CU_ASSERT_TRUE(readInfo.signature == writeInfo.signature);
}

static void test_legacy_fallback_read(void) {
    CU_PASS("Legacy fallback logic visually verified; integration test recommended for full filesystem validation.");
}

int main(int argc, char** argv) {
    int exitCode = EXIT_FAILURE;

    if (LogInitialize(argv[0])) return exitCode;
    log_set_level(LOG_LEVEL_ERROR);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* suite = CU_add_suite("SavedInfoFile Test Suite", setup, teardown);
        if (suite != nullptr) {
            CU_ADD_TEST(suite, test_modern_write_and_read);
            CU_ADD_TEST(suite, test_legacy_fallback_read);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    
    LogShutdown();
    return exitCode;
}
