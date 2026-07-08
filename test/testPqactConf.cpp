#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include "Log.h"
#include "tools/pqact/PqactConfFile.h"

static char tmpConfigPath[] = "/tmp/test_pqact_conf_XXXXXX";

static int setup(void) {
    int fd = mkstemp(tmpConfigPath);
    if (fd == -1) return -1;
    close(fd);
    return 0;
}

static int teardown(void) {
    unlink(tmpConfigPath);
    return 0;
}

// Helper to write raw strings (including actual tabs) to the temp file
static void writeConfig(const char* content) {
    FILE* fp = fopen(tmpConfigPath, "w");
    CU_ASSERT_PTR_NOT_NULL_FATAL(fp);
    fputs(content, fp);
    fclose(fp);
}

static void test_parse_valid_config(void) {
    // Standard valid pqact.conf format using strict tabs
    writeConfig(
        "# This is a comment at the top of the file\n"
        "\n"
        "EXP\t^WMO_.*\\.txt$\tFILE\t-overwrite /tmp/data/\\1\n"
        "ANY\t.*\tNOOP\n"
    );
    
    int parsed_count = ldm::pqact::readConfigFile(tmpConfigPath);
    CU_ASSERT_EQUAL(parsed_count, 2);
}

static void test_parse_invalid_tabs(void) {
    // Classic pqact.conf pitfall: Using spaces instead of tabs
    writeConfig(
        "EXP    ^WMO_.*\\.txt$    FILE    -overwrite /tmp/data/\\1\n"
    );
    
    int parsed_count = ldm::pqact::readConfigFile(tmpConfigPath);
    // The parser should see this as one giant token, fail the token count check (< 3), and skip it
    CU_ASSERT_EQUAL(parsed_count, 0);
}

static void test_parse_bad_regex(void) {
    // Providing a syntactically invalid regular expression
    writeConfig(
        "EXP\t^[unclosed_bracket\tNOOP\n"
    );
    
    int parsed_count = ldm::pqact::readConfigFile(tmpConfigPath);
    // regcomp should fail and reject the entry
    CU_ASSERT_EQUAL(parsed_count, 0);
}

static void test_parse_invalid_action(void) {
    // Providing a non-existent LDM action
    writeConfig(
        "EXP\t^valid_regex$\tFAKEACTION\t/tmp/data\n"
    );
    
    int parsed_count = ldm::pqact::readConfigFile(tmpConfigPath);
    // atoaction should fail and reject the entry
    CU_ASSERT_EQUAL(parsed_count, 0);
}

int main(int argc, const char* const* argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) return EXIT_FAILURE;

    // Suppress expected error logs from the parser to keep test output clean
    log_set_level(LOG_LEVEL_FATAL); 

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite("PqactConfFile Parser Suite", setup, teardown);
        if (NULL != testSuite) {
            CU_ADD_TEST(testSuite, test_parse_valid_config);
            CU_ADD_TEST(testSuite, test_parse_invalid_tabs);
            CU_ADD_TEST(testSuite, test_parse_bad_regex);
            CU_ADD_TEST(testSuite, test_parse_invalid_action);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            (void) CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    
    LogShutdown();
    return exitCode;
}
