#include "config.h"
#include "Log.h"
#include "Pattern.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_regular_expressions_vetting(void) {
    // Test that leading ".*" is cleanly stripped internally
    Pattern pat1(".*NEXRAD", false);
    CU_ASSERT_STRING_EQUAL(pat1.getEre().c_str(), "NEXRAD");

    // Test that multiple nested layers of ".*" are stripped
    Pattern pat2(".*.*FOO", false);
    CU_ASSERT_STRING_EQUAL(pat2.getEre().c_str(), "FOO");

    // Test that a trailing or mid-string ".*" is preserved perfectly
    Pattern pat3("NEXRAD.*", false);
    CU_ASSERT_STRING_EQUAL(pat3.getEre().c_str(), "NEXRAD.*");

    // Test that a pure catch-all match doesn't get wiped out completely
    Pattern pat4(".*", false);
    CU_ASSERT_TRUE(pat4.isMatchAll());
    CU_ASSERT_STRING_EQUAL(pat4.getEre().c_str(), ".*");
}

static void test_pattern_match_all(void) {
    Pattern pat(".*", false);
    
    CU_ASSERT_TRUE(pat.isMatchAll());
    CU_ASSERT_STRING_EQUAL(pat.getEre().c_str(), ".*");
    CU_ASSERT_TRUE(pat.isMatch("ANYTHING_GOES"));
    CU_ASSERT_TRUE(pat.isMatch(""));
}

static void test_pattern_standard_match(void) {
    Pattern pat("^WMO_[0-9]+", false); // false = Case sensitive
    
    CU_ASSERT_FALSE(pat.isMatchAll());
    CU_ASSERT_TRUE(pat.isMatch("WMO_12345"));
    CU_ASSERT_FALSE(pat.isMatch("WMO_ABC"));
    CU_ASSERT_FALSE(pat.isMatch(" PREFIX_WMO_123")); // Fails due to ^ anchor
}

static void test_pattern_ignore_case(void) {
    Pattern pat("^wmo", true); // true = Ignore case
    
    CU_ASSERT_TRUE(pat.isMatch("WMO_123"));
    CU_ASSERT_TRUE(pat.isMatch("wmo_123"));
}

static void test_pattern_invalid_regex(void) {
    bool caughtException = false;
    try {
        Pattern pat("[a-z", false); // Unclosed bracket
    } catch (const std::regex_error&) {
        caughtException = true;
    }
    CU_ASSERT_TRUE(caughtException);
}

int main(int argc, char** argv) {
    int exitCode = EXIT_FAILURE;
    
    if (LogInitialize(argv[0])) {
        std::fprintf(stderr, "Couldn't open logging system\n");
        return exitCode;
    }

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* suite = CU_add_suite("Pattern and Regex Suite", setup, teardown);
        if (suite != nullptr) {
            CU_ADD_TEST(suite, test_regular_expressions_vetting);
            CU_ADD_TEST(suite, test_pattern_match_all);
            CU_ADD_TEST(suite, test_pattern_standard_match);
            CU_ADD_TEST(suite, test_pattern_ignore_case);
            CU_ADD_TEST(suite, test_pattern_invalid_regex);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return exitCode;
}
