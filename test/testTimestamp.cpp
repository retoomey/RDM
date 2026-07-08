#include "config.h"
#include "Timestamp.h"
#include "Log.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_timestamp_arithmetic(void) {
    Timestamp t1(1000, 500000);
    Timestamp t2(2000, 600000);
    
    Timestamp res_add = t1 + t2;
    CU_ASSERT_EQUAL(res_add.tv_sec, 3001);
    CU_ASSERT_EQUAL(res_add.tv_usec, 100000);

    Timestamp res_sub = t2 - t1;
    CU_ASSERT_EQUAL(res_sub.tv_sec, 1000);
    CU_ASSERT_EQUAL(res_sub.tv_usec, 100000);

    double dres = res_sub.AsSeconds();
    CU_ASSERT_DOUBLE_EQUAL(dres, 1000.1, 0.0001);
}

static void test_timestamp_increments(void) {
    Timestamp t1(1000, 999999);
    t1.IncrementMicrosecond();
    CU_ASSERT_EQUAL(t1.tv_sec, 1001);
    CU_ASSERT_EQUAL(t1.tv_usec, 0);

    t1.DecrementMicrosecond();
    CU_ASSERT_EQUAL(t1.tv_sec, 1000);
    CU_ASSERT_EQUAL(t1.tv_usec, 999999);
}

static void test_timestamp_format_parse(void) {
    std::string time_str = "20260527T150201.123456";
    
    auto opt_ts = Timestamp::Parse(time_str);
    CU_ASSERT_TRUE(opt_ts.has_value());
    CU_ASSERT_EQUAL(opt_ts->tv_usec, 123456);

    std::string formatted = opt_ts->ToString();
    CU_ASSERT_STRING_EQUAL(formatted.c_str(), time_str.c_str());
}

static void test_timestamp_comparisons(void) {
    Timestamp t1(1000, 500000);
    Timestamp t2(1000, 500000);
    Timestamp t3(1000, 500001);
    Timestamp t4(1001, 0);

    CU_ASSERT_TRUE(t1 == t2);
    CU_ASSERT_TRUE(t1 != t3);
    CU_ASSERT_TRUE(t1 < t3);
    CU_ASSERT_TRUE(t3 < t4);
    CU_ASSERT_TRUE(t4 >= t1);
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;
    if (LogInitialize(argv[0])) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("Modern Timestamp Suite", setup, teardown);
            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_timestamp_arithmetic) &&
                    CU_ADD_TEST(testSuite, test_timestamp_increments) &&
                    CU_ADD_TEST(testSuite, test_timestamp_format_parse) &&
                    CU_ADD_TEST(testSuite, test_timestamp_comparisons)) {
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
