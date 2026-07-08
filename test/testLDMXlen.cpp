#include "config.h"
#include "Log.h"
#include "NetworkFactory.h"
#include "FeedType.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_xlen_prod_info(void) {
    auto serializer = NetworkFactory::CreateSerializer();
    
    ProdInfo info;
    info.origin = "host";
    info.ident = "data";
    
    // FIX: Access via pointer
    size_t len = serializer->GetEncodedInfoSize(info);
    CU_ASSERT_EQUAL(len, 52);
    
    info.ident = "data1";
    len = serializer->GetEncodedInfoSize(info);
    CU_ASSERT_EQUAL(len, 56);
}

static void test_xlen_product(void) {
    auto serializer = NetworkFactory::CreateSerializer();
    
    Product prod;
    prod.info.origin = "host";
    prod.info.ident = "data";
    prod.info.sz = 10;
    
    // FIX: Access via pointer
    size_t len = serializer->GetEncodedSize(prod);
    CU_ASSERT_EQUAL(len, 62);
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("XDR Sizing Test Suite", setup, teardown);
            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_xlen_prod_info) &&
                    CU_ADD_TEST(testSuite, test_xlen_product)) {
                    
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
