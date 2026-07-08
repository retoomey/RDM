#define _DEFAULT_SOURCE
#include "config.h"
#include "FeedType.h"
#include "Signature.h"
#include "Log.h"
#include "Timestamp.h"
#include "ProdClass.h"
#include "ProdSpec.h"

#include <climits>
#include <string>

#include <libgen.h>
#include <stddef.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <unistd.h>
#include <stdlib.h>

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_ProdSpec_ToString(void)
{
    ProdSpec ps;
    const char expect[] = "{HDS, \"foo\"}";
    ps.feedtype = HDS;
    ps.pattern = "foo";
    std::string result = ps.ToString();
    CU_ASSERT_STRING_EQUAL(result.c_str(), expect);
}

static void test_ts_format(void)
{
    Timestamp ts = Timestamp::NONE;
    CU_ASSERT_STRING_EQUAL(ts.ToString().c_str(), "TS_NONE");
    ts = Timestamp::ZERO;
    CU_ASSERT_STRING_EQUAL(ts.ToString().c_str(), "TS_ZERO");
    ts = Timestamp::ENDT;
    CU_ASSERT_STRING_EQUAL(ts.ToString().c_str(), "TS_ENDT");
    ts = Timestamp(123456789, 123456);
    CU_ASSERT_STRING_EQUAL(ts.ToString().c_str(), "19731129T213309.123456");
}

static void test_ProdClass_ToString(void)
{
    ProdClass pc;
    const char   expect[] =
            "TS_ZERO TS_ENDT {{HDS, \"foo\"},{IDS|DDPLUS, \"bar\"}}";

    ProdSpec ps1;
    ps1.feedtype = HDS;
    ps1.pattern = "foo";

    ProdSpec ps2;
    ps2.feedtype = IDS | DDPLUS;
    ps2.pattern = "bar";

    pc.from_sec = Timestamp::ZERO.tv_sec;
    pc.from_usec = Timestamp::ZERO.tv_usec;
    pc.to_sec = Timestamp::ENDT.tv_sec;
    pc.to_usec = Timestamp::ENDT.tv_usec;

    pc.specs.push_back(ps1);
    pc.specs.push_back(ps2);

    std::string result = pc.ToString();
    CU_ASSERT_STRING_EQUAL(result.c_str(), expect);
}

static void test_signature_formatting(void)
{
    std::string expectedHex = "00112233445566778899aabbccddeeff";
    auto sigOpt = Signature::Parse(expectedHex);
    CU_ASSERT_TRUE(sigOpt.has_value());
    if (sigOpt) {
        std::string result = sigOpt->ToString();
        CU_ASSERT_STRING_EQUAL(result.c_str(), expectedHex.c_str());
    }

    auto badOpt = Signature::Parse("not_a_valid_hex_string");
    CU_ASSERT_FALSE(badOpt.has_value());
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (LogInitialize(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_ProdSpec_ToString) &&
                    CU_ADD_TEST(testSuite, test_ts_format) &&
                    CU_ADD_TEST(testSuite, test_ProdClass_ToString) &&
                    CU_ADD_TEST(testSuite, test_signature_formatting)) {
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
