#include "NetworkUtils.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace rdm;

void test_origin_update() {
    // 1. Test appending to a clean origin
    std::string original = "12345_baseline_origin";
    std::string host = "upstream.host.edu";
    std::string updated = network::AppendUpstreamHostToOrigin(original, host.c_str());
    
    if (updated != "12345_baseline_origin_v_upstream.host.edu") {
        std::cerr << "Failed basic append: " << updated << std::endl;
        exit(1);
    }

    // 2. Test replacing an already existing upstream host ID
    std::string already_appended = "12345_baseline_origin_v_old.host.edu";
    std::string updated_again = network::AppendUpstreamHostToOrigin(already_appended, host.c_str());
    
    if (updated_again != "12345_baseline_origin_v_upstream.host.edu") {
        std::cerr << "Failed replacement: " << updated_again << std::endl;
        exit(1);
    }

    std::cout << "Origin update tests passed!" << std::endl;
}

int main() {
    test_origin_update();
    return 0;
}
