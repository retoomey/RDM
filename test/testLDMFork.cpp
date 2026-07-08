/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ldmfork_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the `ldmfork` module.
 */
#include "config.h"
#include "ProcessUtil.h"
#include "Log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

using namespace rdm;

static int setup(void) {
    return 0;
}

static int teardown(void) {
    return 0;
}

static void test_open_on_dev_null_if_closed(void) {
    int status = close(STDERR_FILENO);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    
    status = os::openOnDevNullIfClosed(STDERR_FILENO, O_RDWR);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_TRUE_FATAL(log_stderr_is_open());
    CU_ASSERT_TRUE_FATAL(fcntl(STDERR_FILENO, F_GETFD) >= 0);
}

static void test_ensure_close_on_exec(void) {
    // Open a temporary file descriptor
    int fd = open("/dev/null", O_RDONLY);
    CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

    // Verify FD_CLOEXEC is NOT set by default on a standard open()
    int flags = fcntl(fd, F_GETFD);
    CU_ASSERT_NOT_EQUAL_FATAL(flags, -1);
    CU_ASSERT_FALSE(flags & FD_CLOEXEC);

    // Apply the function
    int status = os::ensureCloseOnExec(fd);
    CU_ASSERT_EQUAL(status, 0);

    // Verify FD_CLOEXEC is now set
    flags = fcntl(fd, F_GETFD);
    CU_ASSERT_NOT_EQUAL_FATAL(flags, -1);
    CU_ASSERT_TRUE(flags & FD_CLOEXEC);

    close(fd);
}

static void test_ldmfork(void) {
    pid_t pid = os::ldmFork();
    CU_ASSERT_NOT_EQUAL_FATAL(pid, -1);

    if (pid == 0) {
        // Child process: just exit cleanly
        exit(0);
    } else {
        // Parent process: wait for child
        int child_status;
        pid_t wpid = waitpid(pid, &child_status, 0);
        
        CU_ASSERT_EQUAL(wpid, pid);
        CU_ASSERT_TRUE(WIFEXITED(child_status));
        CU_ASSERT_EQUAL(WEXITSTATUS(child_status), 0);
    }
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) {
        LogSyserr("Couldn't initialize logging module");
        exitCode = EXIT_FAILURE;
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("LDM Fork Test Suite", setup, teardown);
            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_open_on_dev_null_if_closed) &&
                    CU_ADD_TEST(testSuite, test_ensure_close_on_exec) &&
                    CU_ADD_TEST(testSuite, test_ldmfork)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }
            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
        LogShutdown();
    }
    return exitCode;
}
