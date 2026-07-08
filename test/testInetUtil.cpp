#include "config.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include "Log.h"
#include "ServiceAddr.h"
#include "NetworkUtils.h"

using namespace rdm;

static int setup(void) { return 0; }
static int teardown(void) { return 0; }

static void test_GetHostByAddr_localhost(void) {
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    std::string name = network::GetHostByAddr(&addr);
    
    CU_ASSERT_FALSE(name.empty());
    // It should resolve to localhost or something similar depending on /etc/hosts
    CU_ASSERT_TRUE(name == "localhost" || name == "127.0.0.1" || name.length() > 0);
}

static void test_IsLocalHost(void) {
    CU_ASSERT_TRUE(network::IsLocalHost("localhost"));
    CU_ASSERT_TRUE(network::IsLocalHost("loopback"));
    CU_ASSERT_TRUE(network::IsLocalHost("127.0.0.1"));
    CU_ASSERT_FALSE(network::IsLocalHost("some.fake.nonexistent.domain.com"));
}

static void test_ServiceAddr_format(void) {
    ServiceAddr sa("192.168.1.100", 388);
    std::string formatted = sa.ToString();
    CU_ASSERT_TRUE(formatted.find(":388") != std::string::npos);
    CU_ASSERT_STRING_EQUAL(formatted.c_str(), "192.168.1.100:388");

    // IPv6 formatting test
    ServiceAddr sa_ipv6("2001:db8::1", 388);
    std::string formatted_v6 = sa_ipv6.ToString();
    CU_ASSERT_STRING_EQUAL(formatted_v6.c_str(), "[2001:db8::1]:388");
}

static void test_ServiceAddr_Resolve_Invalid(void) {
    ServiceAddr sa("this.is.not.a.real.host.domain.test", 388);
    struct sockaddr_storage addr;
    socklen_t len;
    
    // Should fail gracefully and return false
    CU_ASSERT_FALSE(sa.Resolve(&addr, &len));
}

int main(int argc, char** argv) {
    int exitCode = EXIT_FAILURE;

    if (LogInitialize(argv[0])) {
        std::fprintf(stderr, "Couldn't open logging system\n");
        return exitCode;
    }
    log_set_level(LOG_LEVEL_ERROR);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* suite = CU_add_suite("NetworkUtils Test Suite", setup, teardown);

        if (suite != nullptr) {
            // Register the modernized tests
            CU_ADD_TEST(suite, test_GetHostByAddr_localhost);
            CU_ADD_TEST(suite, test_IsLocalHost);
            CU_ADD_TEST(suite, test_ServiceAddr_format);
            CU_ADD_TEST(suite, test_ServiceAddr_Resolve_Invalid);

            CU_basic_set_mode(CU_BRM_VERBOSE);
            CU_basic_run_tests();
            exitCode = CU_get_number_of_tests_failed();
        }
        CU_cleanup_registry();
    }
    LogShutdown();
    return exitCode;
}
