#include "FileUtil.h"

#include "config.h"
#include "Log.h"

#include <iostream>
#include <cstring>
#include <cstdlib>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

using namespace rdm;

int main(int argc, char** argv) {
    const char* testFile = "fs_test_fixture.tmp";

    if (LogInitialize(argv[0])) {
        std::cerr << "Logging init failed\n";
        return EXIT_FAILURE;
    }

    // 1. Create a local dummy file fixture to guarantee a valid target path
    int create_fd = ::open(testFile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (create_fd == -1) {
        int err = errno;
        LogError("Failed to create test fixture '{}': {}", testFile, std::strerror(err));
        LogShutdown();
        return EXIT_FAILURE;
    }
    // Write a tiny bit of dummy data just to have a non-empty file descriptor
    if (::write(create_fd, "LDM", 3) == -1) {
        LogWarning("Failed to write to test fixture, proceeding anyway.");
    }
    ::close(create_fd);

    // 2. Open the fixture for reading to pass into fsStats
    int fd = ::open(testFile, O_RDONLY, 0);
    if (fd == -1) {
        int err = errno;
        LogError("Open failed on test fixture '{}': {}", testFile, std::strerror(err));
        std::remove(testFile);
        LogShutdown();
        return EXIT_FAILURE;
    }

    off_t totalBytes = 0;
    off_t availBytes = 0;

    // 3. Execute the call
    int status = fsStats(fd, &totalBytes, &availBytes);
    
    // Clean up the descriptor immediately
    ::close(fd);

    // 4. Assert and validate results
    if (status != 0) {
        LogError("fsStats failed for fixture: {}", std::strerror(status));
        std::remove(testFile);
        LogShutdown();
        return EXIT_FAILURE;
    }

    // Sanity check: A valid filesystem partition should report a positive size
    if (totalBytes <= 0) {
        LogError("Sanity check failed: Reported partition size is invalid ({} bytes)", (long)totalBytes);
        std::remove(testFile);
        LogShutdown();
        return EXIT_FAILURE;
    }

    // 5. Clean up the disk footprint so the test is fully repeatable
    std::remove(testFile);

    LogNotice("FileUtil fsStats automated test passed! (Size: {} MB, Avail: {} MB)", 
               (long)(totalBytes / 1024 / 1024), (long)(availBytes / 1024 / 1024));
    
    LogShutdown();
    return EXIT_SUCCESS;
}
