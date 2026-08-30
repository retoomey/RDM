#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "Log.h"
#include "PqactParser.h"
#include "PqactContext.h"
#include "ProcessManager.h"

using namespace rdm;

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

static void writeConfig(const char* content) {
    FILE* fp = fopen(tmpConfigPath, "w");
    CU_ASSERT_PTR_NOT_NULL_FATAL(fp);
    fputs(content, fp);
    fclose(fp);
}

static void test_parse_all_action_types(void) {
    writeConfig(
        "# Advanced pqact.conf with spaces, tabs, and continuations\n"
        "ANY  .*  NOOP\n"
        "EXP  pattern  FILE  -overwrite /tmp/data\n"
        "EXP  pattern  STDIOFILE  /tmp/stdio\n"
        "EXP  pattern  DBFILE  /tmp/db\n"
        "EXP  pattern  EXEC  echo \"hello \\\n"
        "                       world\"\n"
        "EXP  pattern  PIPE  cat > /tmp/out\n"
    );

    ProcessManager procMgr;
    pqact::PqactContext ctx(nullptr, 1024, procMgr);
    
    bool parseSuccess = pqact::PqactParser::Parse(tmpConfigPath, ctx, ctx.config);
    
    // Check success, and ABORT the test early if it fails to prevent segfaults
    CU_ASSERT_TRUE(parseSuccess);
    if (!parseSuccess) {
        LogError("Test aborted: Parser returned false");
        return; 
    }

    CU_ASSERT_EQUAL(ctx.config.entries.size(), 6);
    if (ctx.config.entries.size() != 6) {
        LogError("Test aborted: Expected 6 entries, got %zu", ctx.config.entries.size());
        return;
    }

    // Verify actions mapped correctly
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[0]->action->GetName(), "noop");
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[1]->action->GetName(), "file");
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[2]->action->GetName(), "stdiofile");
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[3]->action->GetName(), "dbfile");
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[4]->action->GetName(), "exec");
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[5]->action->GetName(), "pipe");

    // Verify continuation line reassembled the args properly
    CU_ASSERT_STRING_EQUAL(ctx.config.entries[4]->args.c_str(), "echo \"hello                        world\"");
}

int main(int argc, const char* const* argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) return EXIT_FAILURE;
    log_set_level(LOG_LEVEL_FATAL);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite("PqactConfFile Parser Suite", setup, teardown);
        if (NULL != testSuite) {
            CU_ADD_TEST(testSuite, test_parse_all_action_types);
            CU_basic_set_mode(CU_BRM_VERBOSE);
            (void) CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return exitCode;
}
