#include "config.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdexcept>
#include <cstdio>
#include "Wordexp.h"
#include "Log.h"

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_wordexp_basic(void) {
    Wordexp we("ALLOW  ANY  ^.*$");
    CU_ASSERT_EQUAL(we.getArgc(), 3);
    CU_ASSERT_STRING_EQUAL(we.getArgv()[0], "ALLOW");
    CU_ASSERT_STRING_EQUAL(we.getArgv()[1], "ANY");
    CU_ASSERT_STRING_EQUAL(we.getArgv()[2], "^.*$");
    CU_ASSERT_PTR_NULL(we.getArgv()[3]); // Must be null terminated for execvp
}

static void test_wordexp_quotes(void) {
    Wordexp we("EXEC \"this is a single string\"");
    CU_ASSERT_EQUAL(we.getArgc(), 2);
    CU_ASSERT_STRING_EQUAL(we.getArgv()[0], "EXEC");
    CU_ASSERT_STRING_EQUAL(we.getArgv()[1], "this is a single string");
    CU_ASSERT_PTR_NULL(we.getArgv()[2]);
}

static void test_wordexp_comments(void) {
    // Test mid-line comments
    Wordexp we1("ACCEPT ANY .* # This is a comment");
    CU_ASSERT_EQUAL(we1.getArgc(), 3);
    CU_ASSERT_STRING_EQUAL(we1.getArgv()[0], "ACCEPT");
    CU_ASSERT_STRING_EQUAL(we1.getArgv()[1], "ANY");
    CU_ASSERT_STRING_EQUAL(we1.getArgv()[2], ".*");

    // Test line starting with comments
    Wordexp we2("   # Just a comment");
    CU_ASSERT_EQUAL(we2.getArgc(), 0);
    CU_ASSERT_PTR_NULL(we2.getArgv()[0]);
}

static void test_wordexp_syntax_error(void) {
    bool caughtException = false;
    try {
        Wordexp we("EXEC \"unclosed string");
    } catch (const std::invalid_argument&) {
        caughtException = true;
    }
    CU_ASSERT_TRUE(caughtException);
}

static void test_wordexp_empty(void) {
    Wordexp we("   \t  ");
    CU_ASSERT_EQUAL(we.getArgc(), 0);
    CU_ASSERT_PTR_NULL(we.getArgv()[0]);
}

int main(int argc, char** argv) {
    int exitCode = EXIT_FAILURE;

    if (LogInitialize(argv[0])) {
        std::fprintf(stderr, "Couldn't open logging system\n");
        return exitCode;
    }

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* suite = CU_add_suite("Word Expansion Test Suite", setup, teardown);
        if (suite != nullptr) {
            CU_ADD_TEST(suite, test_wordexp_basic);
            CU_ADD_TEST(suite, test_wordexp_quotes);
            CU_ADD_TEST(suite, test_wordexp_comments);
            CU_ADD_TEST(suite, test_wordexp_syntax_error);
            CU_ADD_TEST(suite, test_wordexp_empty);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return exitCode;
}
