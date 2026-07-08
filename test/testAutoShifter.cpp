#include "config.h"
#include "AutoShifter.h"
#include "Log.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <thread>
#include <chrono>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_initialization(void) {
    AutoShifter shifter(true, 2, 0.05); // Primary, 2 servers, 50ms interval
    CU_ASSERT_FALSE(shifter.ShouldSwitch());
}

static void test_invalid_ldm_count(void) {
    AutoShifter shifter(true, 2, 0.05);
    CU_ASSERT_EQUAL(shifter.SetLdmCount(0), EINVAL);
}

static void test_legacy_switch_heuristic(void) {
    // Instantiate an independent tracker with a tiny 10ms interval
    AutoShifter shifter(true, 3, 0.01); 

    // Simulate losing product races (success = 0)
    shifter.Process(0, 1024);
    shifter.Process(0, 1024);

    // Sleep past the 2x evaluation window (2 * 10ms = 20ms) to unlock calculation execution path
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    // Send a final message to pass the boundary validation gate
    shifter.Process(0, 1024);

    // Primary has won 0 races while alternates accumulated rejections. 
    // It should immediately trigger a primary-to-alternate switch warning.
    CU_ASSERT_TRUE(shifter.ShouldSwitch());
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("Modernized AutoShifter Suite", setup, teardown);
            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_initialization) &&
                    CU_ADD_TEST(testSuite, test_invalid_ldm_count) &&
                    CU_ADD_TEST(testSuite, test_legacy_switch_heuristic)) {
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
