#include "config.h"
#include "Product.h"
#include "Signature.h"
#include "Log.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

using namespace rdm;

static char tmpFilePath[] = "/tmp/test_prod_info_XXXXXX";
static int tmpFd = -1;

static int setup(void) {
    tmpFd = mkstemp(tmpFilePath);
    if (tmpFd == -1) return -1;
    return 0;
}

static int teardown(void) {
    if (tmpFd != -1) close(tmpFd);
    unlink(tmpFilePath);
    return 0;
}

static void test_modern_lifecycle_and_equality(void) {
    ProdInfo info1;
    info1.arrival.tv_sec = 1600000000;
    info1.arrival.tv_usec = 123456;
    info1.feedtype = IDS | DDPLUS;
    info1.seqno = 12345;
    info1.sz = 1048576;
    info1.origin = "weather.unidata.ucar.edu";
    info1.ident = "WMO_DATA_IDENTIFIER";

    // Use our new parser to safely build the object from a hex string
    auto sigOpt = Signature::Parse("0123456789abcdeffedcba9876543210");
    CU_ASSERT_TRUE(sigOpt.has_value());
    if (sigOpt) {
        info1.signature = *sigOpt;
    }

    ProdInfo info2 = info1;
    CU_ASSERT_TRUE(info1 == info2);

    info2.sz = 2048;
    CU_ASSERT_FALSE(info1 == info2);
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("Prod Info Test Suite", setup, teardown);
            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_modern_lifecycle_and_equality)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }
            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }
    return exitCode;
}
