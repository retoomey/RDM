#include "config.h"
#ifndef _XOPEN_SOURCE
#   define _XOPEN_SOURCE 500
#endif
#include <libgen.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "Product.h"
#include "Log.h"
#include <cstring>

using namespace rdm;

static int setup(void) {
    return 0;
}

static int teardown(void) {
    return 0;
}

static void test_product_equality(void) {
    // 1. Test "Nil" state (default initialization)
    Product nilProd1{};
    Product nilProd2{};
    CU_ASSERT_TRUE(nilProd1 == nilProd2);
    CU_ASSERT_PTR_NULL(nilProd1.data);

    // 2. Test populated products
    Product prod1;
    prod1.info.feedtype = rdm::IDS;
    prod1.info.seqno = 12345;
    prod1.info.sz = 4;
    prod1.info.ident = "WMO_TEST_123";
    prod1.info.origin = "localhost";
    const uint8_t mockData[] = {0xDE, 0xAD, 0xBE, 0xEF};
    prod1.data = mockData;

    // Default copy constructor handles the shallow copy perfectly
    Product prod2 = prod1;

    // Verify deep equality check works
    CU_ASSERT_TRUE(prod1 == prod2);
    
    // 3. Test inequality (modify size and data)
    const uint8_t diffData[] = {0xDE, 0xAD, 0xBE, 0x00};
    prod2.data = diffData;
    CU_ASSERT_FALSE(prod1 == prod2);
}

int main(const int argc, const char* const * argv) {
    int exitCode;
    const char* progname = basename((char*) argv[0]);

    if (LogInitialize(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("Modern Data Product Suite", setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_product_equality)) {
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
