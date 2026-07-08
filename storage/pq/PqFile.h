#pragma once
#include <string>
#include <sys/types.h>

namespace rdm {

class PqFile {
private:
    int fd_{ -1 };
    std::string pathname_;
    size_t pagesz_{ 0 };

public:
    PqFile();
    ~PqFile();

    // Disable copy/move semantics to prevent double-closing file descriptors
    PqFile(const PqFile&) = delete;
    PqFile& operator=(const PqFile&) = delete;

    int open(const std::string& path, bool readOnly);
    int create(const std::string& path, mode_t mode, int pflags);
    int close();
    
    // Deletes the file from disk
    int unlink();

    int fd() const { return fd_; }
    const std::string& path() const { return pathname_; }
    size_t pageSize() const { return pagesz_; }
    bool isOpen() const { return fd_ >= 0; }
};

} // namespace rdm
