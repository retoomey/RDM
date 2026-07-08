#include "config.h"
#include "Log.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "FeedType.h"
#include "NetworkFactory.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

using namespace rdm;

static int verify_product(const ProdInfo& info, const void *datap, void *xprod, size_t size, void *arg) {
    int* count = static_cast<int*>(arg);
    (*count)++;
    
    if (std::strncmp(info.ident.c_str(), "test_product_", 13) != 0) {
        LogError("Product ident mismatch: {}", info.ident);
        return static_cast<int>(PqStatus::Corrupt);
    }
    
    if (std::strncmp(static_cast<const char*>(datap), "Hello Modern", 12) != 0) {
        LogError("Product data mismatch!");
        return static_cast<int>(PqStatus::Corrupt);
    }
    
    return 0;
}

int main(int argc, char** argv) {
    const char* testQueue = "test_queue.pq";

    if (LogInitialize(argv[0])) {
        std::cerr << "Logging init failed\n";
        return EXIT_FAILURE;
    }

    int test_modes[] = { PqFlags::Default, PqFlags::NoMap, PqFlags::MapRgns };
    const char* mode_names[] = { "FlatMmapMapper", "FileRegionMapper", "ChunkedMmapMapper" };

    char dummy_ident1[] = "test_product_12345";
    char dummy_ident2[] = "test_product_67890";
    char dummy_origin[] = "localhost";
    char dummy_data[] = "Hello Modern C++ LDM Queue!";

    for (int i = 0; i < 3; i++) {
        LogNotice("=== Testing {} ===", mode_names[i]);

        // Dynamically load the PQ plugin via the factory
        auto pq_write = StorageFactory::Create(NetworkFactory::CreateSerializer());
        
        int status = pq_write->create(testQueue, 0666, test_modes[i], 0, 1000000, 1000);
        if (status) {
            LogError("Failed to create test queue: {}", std::strerror(status));
            return EXIT_FAILURE;
        }

        Product prod;
        prod.info.ident = dummy_ident1;
        prod.info.origin = dummy_origin;
        prod.info.feedtype = 1;
        prod.info.seqno = 0;
        prod.info.sz = sizeof(dummy_data);
        prod.data = reinterpret_cast<const uint8_t*>(dummy_data);
        prod.info.arrival = Timestamp::Now();
        prod.info.signature.fill(0);
        prod.info.signature[0] = 1;

        status = pq_write->insert(prod);
        if (status) {
            LogError("Failed to insert product 1: {}", status);
            return EXIT_FAILURE;
        }

        prod.info.seqno++;
        prod.info.ident = dummy_ident2;
        prod.info.signature[0] = 2;

        status = pq_write->insert(prod);
        if (status) {
            LogError("Failed to insert product 2: {}", status);
            return EXIT_FAILURE;
        }

        pq_write->close();

        // Dynamically load the read-side PQ plugin via the factory
        auto pq_read = StorageFactory::Create(NetworkFactory::CreateSerializer());
        status = pq_read->open(testQueue, test_modes[i] | PqFlags::ReadOnly);
        if (status) {
            LogError("Failed to open existing queue: {}", std::strerror(status));
            return EXIT_FAILURE;
        }

        ProdClass clss;
        clss.from_sec = 0;
        clss.from_usec = 0;
        clss.to_sec = 0x7fffffff;
        clss.to_usec = 999999;
        ProdSpec spec;
        spec.feedtype = ANY;
        spec.pattern = ".*";
        clss.specs.push_back(spec);

        int match_count = 0;
        auto cursor = pq_read->CreateCursor();
        while ((status = cursor->sequence(Match::GreaterThan, clss, verify_product, &match_count)) == 0) {
        }

        if (status != static_cast<int>(PqStatus::End)) {
            LogError("Failed to sequence queue: {}", std::strerror(status));
            return EXIT_FAILURE;
        }

        if (match_count != 2) {
            LogError("Expected 2 products, found {}", match_count);
            return EXIT_FAILURE;
        }

        pq_read->close();
        std::remove(testQueue);
        LogNotice("{} passed!", mode_names[i]);
    }

    LogNotice("All ProductQueue mapper tests passed!");
    LogShutdown();
    return EXIT_SUCCESS;
}
