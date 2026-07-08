// Toomey May 2026
// Write a test for ldm.conf using the LdmConfFile
// definition. This safely replaces all the C manual stuff 
// with modern C++ and ensures nothing breaks.

#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "Log.h"
#include "ConfParser.h"
#include "AclManager.h"

using namespace rdm;

static char tmpConfigPath[] = "/tmp/test_ldmd_conf_XXXXXX";
static char tmpIncludePath[] = "/tmp/test_ldmd_include_XXXXXX";

static int setup(void) {
    int fd = mkstemp(tmpConfigPath);
    if (fd == -1) return -1;
    close(fd);
    fd = mkstemp(tmpIncludePath);
    if (fd == -1) {
        unlink(tmpConfigPath);
        return -1;
    }
    close(fd);
    return 0;
}

static int teardown(void) {
    unlink(tmpConfigPath);
    unlink(tmpIncludePath);
    return 0;
}

static void writeCustomConfig(const char* path, const char* content) {
    FILE* fp = fopen(path, "w");
    CU_ASSERT_PTR_NOT_NULL_FATAL(fp);
    fputs(content, fp);
    fclose(fp);
}

static void writeConfig(const char* content) {
    writeCustomConfig(tmpConfigPath, content);
}

static void test_parse_allow_rules(void) {
    writeConfig("ALLOW\tANY\t^127\\.0\\.0\\.1$|\t^.*$\n");
    ServerConfig config = ConfParser::Parse(tmpConfigPath);
    
    CU_ASSERT_TRUE(config.RequiresServer());
    CU_ASSERT_EQUAL(config.allowRules.size(), 1);
    
    // Create a local instance instead of initializing a singleton
    AclManager aclManager(config.allowRules, config.acceptRules);
    
    // Call methods directly on the instance
    FeedType allowed = aclManager.GetAllowed("localhost", "127.0.0.1", ANY);
    CU_ASSERT_EQUAL(allowed, ANY);
}

static void test_parse_exec_rules(void) {
    writeConfig("EXEC\t\"pqact -i 15\"\n");
    
    ServerConfig config = ConfParser::Parse(tmpConfigPath);
    CU_ASSERT_FALSE(config.RequiresServer());
    CU_ASSERT_EQUAL(config.execRules.size(), 1);
    CU_ASSERT_EQUAL(config.execRules[0].command.getArgc(), 3);
    CU_ASSERT_STRING_EQUAL(config.execRules[0].command.getArgv()[0], "pqact");
}

static void test_parse_complex_config(void) {
    writeCustomConfig(tmpIncludePath,
        "   # A comment at the top\n"
        "ALLOW \t IDS|DDPLUS   ^10\\.0\\.0\\..*$  # This is a comment\n"
    );

    char primaryBuffer[2048];
    snprintf(primaryBuffer, sizeof(primaryBuffer),
        "# Master config\n"
        "EXEC  \"pqact -f ANY -p \\\".*\\\" /opt/ldm/etc/pqact.conf\"\n"
        "ACCEPT\t HDS \t ^SA/.* \t ^192\\.168\\..*\n"
        "REQUEST  WMO \t \".*\"\t  upstream.host.com:5555 \n"
        "INCLUDE \"%s\"\n",
        tmpIncludePath
    );
    writeConfig(primaryBuffer);

    ServerConfig config = ConfParser::Parse(tmpConfigPath);
    CU_ASSERT_TRUE(config.RequiresServer());
    CU_ASSERT_EQUAL(config.execRules.size(), 1);
    CU_ASSERT_EQUAL(config.acceptRules.size(), 1);
    CU_ASSERT_EQUAL(config.requestRules.size(), 1);
    CU_ASSERT_EQUAL(config.allowRules.size(), 1);
    
    // Create a local instance
    AclManager aclManager(config.allowRules, config.acceptRules);
    
    // Call methods directly on the instance
    FeedType allowed = aclManager.GetAllowed("internal-router", "10.0.0.5", IDS);
    CU_ASSERT_EQUAL(allowed, IDS);
}

int main(int argc, const char* const* argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) return EXIT_FAILURE;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite("LdmConfFile Parser Suite", setup, teardown);
        if (NULL != testSuite) {
            CU_ADD_TEST(testSuite, test_parse_allow_rules);
            CU_ADD_TEST(testSuite, test_parse_exec_rules);
            CU_ADD_TEST(testSuite, test_parse_complex_config);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            (void) CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return exitCode;
}
