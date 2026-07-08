#pragma once

#include <cstddef>
#include <string>

#include <sys/types.h>
#include <unistd.h>

namespace rdm {
/**
 * @brief Opens a file, safely creating all necessary parent directories if they don't exist.
 * @param path The full or relative path to the file.
 * @param flags The open() flags (e.g., O_WRONLY | O_CREAT).
 * @param mode The file permissions if created (e.g., 0666).
 * @return A valid file descriptor, or -1 on error.
 */
int
OpenWithMkdirs(const std::string& path, int flags, mode_t mode = 0666);

/**
 * @brief Checks if a directory is accessible, optionally creating it.
 * @param path The directory path to check.
 * @param create If true, creates the directory tree if it does not exist.
 * @return 0 on success, or -1 on error.
 */
int
EnsureDirectoryAccess(const std::string& path, bool create = true);

/**
 * @brief Gets filesystem statistics (total and available space).
 * @return 0 on success, or standard errno on failure.
 */
int
fsStats(int fd, off_t * fs_szp, off_t * remp);

constexpr size_t M_RND_UNIT   = sizeof(double);
constexpr size_t MIN_RGN_SIZE = M_RND_UNIT;

inline constexpr size_t
roundUp(size_t x, size_t unit)
{
  return (((x) + (unit) - 1) / (unit)) * (unit);
}

inline constexpr size_t
mRoundUp(size_t x)
{
  return roundUp(x, M_RND_UNIT);
}

inline constexpr size_t
mRoundDown(size_t x)
{
  return x - (x % M_RND_UNIT);
}

/** @brief Returns the system page size */
long
pagesize();

/** @brief Truncates/extends a file to the specified length */
int
fgrow(int fd, off_t len, int sparse);

/** @brief Checks if a POSIX advisory lock exists on the file region */
pid_t
fd_isLocked(int fd, short l_type, off_t offset, short l_whence, size_t extent);

/** @brief Applies or removes a POSIX advisory lock on the file region */
int
fd_lock(int fd, int cmd, short l_type, off_t offset, short l_whence, size_t extent);

/** @brief Wraps mmap() to map a file descriptor into memory */
int
mapwrap(int fd, off_t offset, size_t extent, int prot, int mflags, void ** ptrp);

/** @brief Wraps munmap() to release a memory-mapped region */
int
unmapwrap(void * ptr, off_t offset, size_t extent, int mflags);
}
