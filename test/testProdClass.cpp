#include "config.h"
#include "ProdClass.h"
#include "Log.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_clss_scrunch(void) {
    ProdClass pc;
    pc.specs.push_back({NONE, ".*"});
    pc.specs.push_back({HDS, "^SA.*"});

    CU_ASSERT_EQUAL(pc.specs.size(), 2);
    
    pc.Optimize();
    
    CU_ASSERT_EQUAL(pc.specs.size(), 1);
    CU_ASSERT_EQUAL(pc.specs[0].feedtype, HDS);
}

static void test_clss_intersect_disjoint_times(void) {
    ProdClass pc1;
    pc1.from_sec = 1000; pc1.from_usec = 0;
    pc1.to_sec = 2000; pc1.to_usec = 0;
    pc1.specs.push_back({ANY, ".*"});

    ProdClass pc2;
    pc2.from_sec = 3000; pc2.from_usec = 0;
    pc2.to_sec = 4000; pc2.to_usec = 0;
    pc2.specs.push_back({ANY, ".*"});

    ProdClass intersection;
    bool status = pc1.Intersect(pc2, intersection);
    
    // Should be false because the time blocks do not intersect
    CU_ASSERT_FALSE(status);
}

static void test_prodInClass(void) {
    ProdClass pc;
    pc.from_sec = 0;
    pc.from_usec = 0;
    pc.to_sec = 0x7fffffff;
    pc.to_usec = 999999;
    pc.specs.push_back({IDS | DDPLUS, "^WMO.*"});

    ProdInfo info;
    info.arrival.tv_sec = 100; // in range
    info.arrival.tv_usec = 0;
    info.feedtype = IDS;
    info.ident = "WMO_DATA_123";

    // Positive Match
    CU_ASSERT_TRUE(pc.Contains(info));

    // Negative Match (Bad Ident)
    info.ident = "NO_MATCH_123";
    CU_ASSERT_FALSE(pc.Contains(info));

    // Negative Match (Bad Feedtype)
    info.ident = "WMO_DATA_123";
    info.feedtype = HDS;
    CU_ASSERT_FALSE(pc.Contains(info));
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("Product Class Test Suite", setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_clss_scrunch) &&
                    CU_ADD_TEST(testSuite, test_clss_intersect_disjoint_times) &&
                    CU_ADD_TEST(testSuite, test_prodInClass)) {
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
