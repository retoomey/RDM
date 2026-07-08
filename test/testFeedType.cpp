#include "config.h"
#include "FeedType.h" // Brings in FeedType.h and feedtype constants
#include "Log.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_valid_parsing(void) {
    FeedType result;
    int status;

    status = FeedType::Parse("HDS", result);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(result == FT2);

    status = FeedType::Parse("IDS|DDPLUS", result);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(result == (FT3 | DDPLUS));

    status = FeedType::Parse("hds|ids", result);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(result == (FT2 | FT3));

    status = FeedType::Parse("ANY-EXP", result);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(result == (ANY & ~FT30));
}

static void test_invalid_syntax(void) {
    FeedType result;
    int status;

    // We no longer rely on global macros like FEEDTYPE_ERR_UKFT,
    // so we just assert that Parse() returns a non-zero error code.
    status = FeedType::Parse("NOT_A_REAL_FEED", result);
    CU_ASSERT_NOT_EQUAL(status, 0);

    status = FeedType::Parse("(HDS|IDS", result);
    CU_ASSERT_NOT_EQUAL(status, 0);

    status = FeedType::Parse("HDS extra_garbage", result);
    CU_ASSERT_NOT_EQUAL(status, 0);
}

static void test_feedtype_operators(void) {
    FeedType f1 = IDS;
    FeedType f2 = DDPLUS;
    
    FeedType combined = f1 | f2;
    CU_ASSERT_TRUE(combined == (IDS | DDPLUS));
    CU_ASSERT_TRUE((combined & IDS) == IDS);
    CU_ASSERT_TRUE((combined & HDS) == NONE);
    
    // Test explicit boolean evaluation
    CU_ASSERT_TRUE(static_cast<bool>(f1));
    
    FeedType empty = NONE;
    CU_ASSERT_FALSE(static_cast<bool>(empty));
}

static void test_feedtype_tostring(void) {
    FeedType f1 = IDS | DDPLUS;
    CU_ASSERT_STRING_EQUAL(f1.ToString().c_str(), "IDS|DDPLUS");
    
    FeedType any = ANY;
    CU_ASSERT_STRING_EQUAL(any.ToString().c_str(), "ANY");
    
    FeedType none = NONE;
    CU_ASSERT_STRING_EQUAL(none.ToString().c_str(), "NONE");

    // Test a raw hex fallback for an unknown/unmapped bit
    FeedType unknownRaw(0x80000000); // Top bit
    CU_ASSERT_STRING_EQUAL(unknownRaw.ToString().c_str(), "0x80000000");
}

int main(const int argc, const char* const * argv) {
    int exitCode = 1;

    if (LogInitialize(argv[0])) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    } else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite("FeedType Modern Class Suite", setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_valid_parsing) &&
                    CU_ADD_TEST(testSuite, test_invalid_syntax) &&
                    CU_ADD_TEST(testSuite, test_feedtype_operators) &&
                    CU_ADD_TEST(testSuite, test_feedtype_tostring)) {
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
