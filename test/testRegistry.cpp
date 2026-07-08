#include "config.h"
#include "Registry.h"
#include "Log.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>

using namespace rdm;

static const char* TEST_REG_DIR = "/tmp/ldm_test_registry";

static int setup(void) {
    // Ensure a clean slate
    std::string rmCmd = std::string("rm -rf ") + TEST_REG_DIR;
    if (system(rmCmd.c_str()) != 0) {
        return -1;
    }
    if (mkdir(TEST_REG_DIR, 0700) != 0) {
        return -1;
    }
    registry::setDirectory(TEST_REG_DIR);
    return 0;
}

static int teardown(void) {
    registry::reset();
    std::string rmCmd = std::string("rm -rf ") + TEST_REG_DIR;
    if (system(rmCmd.c_str()) != 0) {
        return -1;
    }
    return 0;
}

static void test_typed_api(void) {
    // Test defaults
    CU_ASSERT_STRING_EQUAL(registry::getString(registry::RegistryKey::Hostname).c_str(), "");
    CU_ASSERT_EQUAL(registry::getUint(registry::RegistryKey::Port), 388);
    CU_ASSERT_TRUE(registry::getBool(registry::RegistryKey::AntiDosEnabled));

    // Test overrides
    registry::putString(registry::RegistryKey::Hostname, "weather.host.edu");
    CU_ASSERT_STRING_EQUAL(registry::getString(registry::RegistryKey::Hostname).c_str(), "weather.host.edu");

    registry::putUint(registry::RegistryKey::Port, 6000);
    CU_ASSERT_EQUAL(registry::getUint(registry::RegistryKey::Port), 6000);

    registry::putBool(registry::RegistryKey::AntiDosEnabled, false);
    CU_ASSERT_FALSE(registry::getBool(registry::RegistryKey::AntiDosEnabled));

    // Ensure it wrote to disk and persists across flushes
    registry::flush();
    registry::close();
    
    CU_ASSERT_EQUAL(registry::getUint(registry::RegistryKey::Port), 6000);
}

static void test_dynamic_api(void) {
    // Missing keys should return nullopt
    auto missing = registry::getString("/does/not/exist");
    CU_ASSERT_FALSE(missing.has_value());

    // Spaces in keys are rejected
    CU_ASSERT_FALSE(registry::putString("/bad key/with space", "value"));

    // Valid dynamic insertion
    CU_ASSERT_TRUE(registry::putString("/custom/string_key", "custom_val"));
    
    auto val = registry::getString("/custom/string_key");
    CU_ASSERT_TRUE(val.has_value());
    CU_ASSERT_STRING_EQUAL(val->c_str(), "custom_val");
}

static void test_get_all_values(void) {
    // Populate a mock tree
    registry::putString("/tree/node1", "val1");
    registry::putString("/tree/node2/subA", "val2A");
    registry::putString("/tree/node2/subB", "val2B");

    // Fetch the specific subtree
    auto map = registry::getAllValues("/tree");
    
    CU_ASSERT_EQUAL(map.size(), 3);
    CU_ASSERT_STRING_EQUAL(map["/tree/node1"].c_str(), "val1");
    CU_ASSERT_STRING_EQUAL(map["/tree/node2/subA"].c_str(), "val2A");
    CU_ASSERT_STRING_EQUAL(map["/tree/node2/subB"].c_str(), "val2B");

    // Fetching a single leaf node should act like getString but wrapped in a map
    auto singleMap = registry::getAllValues("/tree/node1");
    CU_ASSERT_EQUAL(singleMap.size(), 1);
    CU_ASSERT_STRING_EQUAL(singleMap["/tree/node1"].c_str(), "val1");
}

static void test_deletion_and_reset(void) {
    // Test targeted deletion
    registry::putString("/todelete/key", "bye");
    CU_ASSERT_TRUE(registry::getString("/todelete/key").has_value());
    
    CU_ASSERT_TRUE(registry::deleteValue("/todelete/key"));
    CU_ASSERT_FALSE(registry::getString("/todelete/key").has_value());

    // Test full reset (wipes XML file entirely)
    registry::putString("/toreset/key", "gone");
    registry::reset();
    
    CU_ASSERT_FALSE(registry::getString("/toreset/key").has_value());
}

int main(int argc, char** argv) {
    int exitCode = EXIT_FAILURE;

    if (LogInitialize(argv[0])) {
        std::fprintf(stderr, "Couldn't initialize logging module\n");
        return exitCode;
    }
    
    log_set_level(LOG_LEVEL_ERROR);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* suite = CU_add_suite("Modern Registry Test Suite", setup, teardown);
        if (suite != nullptr) {
            CU_ADD_TEST(suite, test_typed_api);
            CU_ADD_TEST(suite, test_dynamic_api);
            CU_ADD_TEST(suite, test_get_all_values);
            CU_ADD_TEST(suite, test_deletion_and_reset);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    
    LogShutdown();
    return exitCode;
}
