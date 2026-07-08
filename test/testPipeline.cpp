/** Toomey June 2026
 * Testing raw pipeline speed, or trying to.
 */
#include "IProductStore.h"
#include "StorageFactory.h"
#include "NetworkFactory.h"
#include "Log.h"
#include "Timestamp.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <signal.h>

using namespace rdm;

std::atomic<bool> all_products_received{false};
std::atomic<int> received_count{0};
std::chrono::high_resolution_clock::time_point start_time;
std::chrono::high_resolution_clock::time_point end_time;

int watch_callback(const ProdInfo& info, const void* datap, void* xprod, size_t size, void* arg) {
    int total_expected = *static_cast<int*>(arg);
    int current = ++received_count;
    if (current == 1) {
        start_time = std::chrono::high_resolution_clock::now();
    }
    if (current >= total_expected) {
        end_time = std::chrono::high_resolution_clock::now();
        all_products_received = true;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    LogInitialize(argv[0]);
    log_set_level(LOG_LEVEL_ERROR);
    
    if (argc < 6) {
        std::cerr << "Usage: testPipeline <up.pq> <down.pq> <up_pid> <iterations> <payload_size_kb>\n";
        return 1;
    }
    
    const char* upQueuePath = argv[1];
    const char* downQueuePath = argv[2];
    pid_t upPid = std::stoi(argv[3]);
    const int ITERATIONS = std::stoi(argv[4]);
    const size_t PAYLOAD_SIZE_KB = std::stoul(argv[5]);
    const size_t PAYLOAD_SIZE_BYTES = PAYLOAD_SIZE_KB * 1024;
    
    std::string dynamicPayload(PAYLOAD_SIZE_BYTES, 'A');
    auto serializer = NetworkFactory::CreateSerializer();
    
    auto upQueue = StorageFactory::Create(serializer);
    if (upQueue->open(upQueuePath, PqFlags::Default) != 0) {
        std::cerr << "Failed to open upstream queue.\n";
        return 1;
    }
    
    auto downQueue = StorageFactory::Create(serializer);
    if (downQueue->open(downQueuePath, PqFlags::ReadOnly) != 0) {
        std::cerr << "Failed to open downstream queue.\n";
        return 1;
    }
    
    std::thread watcher([&]() {
        ProdClass clss;
        clss.from_sec = 0; clss.from_usec = 0;
        clss.to_sec = 0x7fffffff; clss.to_usec = 999999;
        clss.specs.push_back({EXP, ".*"});
        
        auto cursor = downQueue->CreateCursor();
        cursor->setCursor(Timestamp::ZERO);
        
        int expected = ITERATIONS;
        while (!all_products_received) {
            int status = cursor->sequence(Match::GreaterThan, clss, watch_callback, &expected);
            if (status == -1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else if (status > 0) {
                std::cerr << "Watcher encountered an error: " << status << "\n";
                break;
            }
        }
    });
    
    std::thread monitor([&]() {
        int last_count = 0;
        int timeouts = 0;
        while (!all_products_received) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int current = received_count.load();
            std::cout << "[Monitor] Received " << current << " / " << ITERATIONS
                      << " (Current Cadence: " << (current - last_count) << " products/sec)\n";
            
            if (current == last_count && current < ITERATIONS) {
                timeouts++;
                if (timeouts >= 3) {
                    std::cout << "[Monitor] Pipe idle detected. Sending explicit SIGCONT to upstream daemon (" << upPid << ")\n";
                    ::kill(upPid, SIGCONT);
                    timeouts = 0;
                }
            } else {
                timeouts = 0;
            }
            last_count = current;
        }
    });
    
    Product prod;
    prod.info.origin = "localhost";
    prod.info.feedtype = EXP;
    prod.info.sz = dynamicPayload.size();
    prod.data = reinterpret_cast<const uint8_t*>(dynamicPayload.c_str());
    prod.info.signature.fill(0);
    
    std::cout << "Starting network-active pipeline test (" << ITERATIONS
              << " products @ " << PAYLOAD_SIZE_KB << " KB each)...\n";
              
    for (int i = 0; i < ITERATIONS; ++i) {
        prod.info.seqno = i;
        prod.info.ident = "pipeline_test_" + std::to_string(i);
        prod.info.arrival = Timestamp::Now();
        
        prod.info.signature[0] = (i & 0xFF);
        prod.info.signature[1] = ((i >> 8) & 0xFF);
        prod.info.signature[2] = ((i >> 16) & 0xFF);
        prod.info.signature[3] = (PAYLOAD_SIZE_KB & 0xFF);
        prod.info.signature[4] = ((PAYLOAD_SIZE_KB >> 8) & 0xFF);
        
        int status = upQueue->insert(prod);
        if (status != 0) {
            std::cerr << "Upstream injection failed at iteration " << i
                      << " with status: " << status << " (" << upQueue->strerror(status) << ")\n";
            all_products_received = true;
            break;
        }
        
        if (i % 50 == 0) {
            ::kill(upPid, SIGCONT);
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
    }
    
    ::kill(upPid, SIGCONT);
    
    watcher.join();
    monitor.join();
    
    std::chrono::duration<double> diff = end_time - start_time;
    double totalMegabytes = (static_cast<double>(dynamicPayload.size() * ITERATIONS)) / (1024.0 * 1024.0);
    
    std::cout << "\n=======================================================\n";
    std::cout << " Benchmark Results (Optimized Core Ingestion Mode)\n";
    std::cout << "-------------------------------------------------------\n";
    std::cout << " Time elapsed:        " << diff.count() << " seconds\n";
    std::cout << " Network Throughput:  " << (ITERATIONS / diff.count()) << " products/sec\n";
    std::cout << " Effective Bandwidth: " << (totalMegabytes / diff.count()) << " MB/sec\n";
    std::cout << "=======================================================\n";
    
    upQueue->close();
    downQueue->close();
    return 0;
}
