#pragma once

#include <sys/types.h>
#include <unistd.h>

namespace rdm {
class PrivilegeManager {
private:
  bool isPrivileged_{ false };
  uid_t realUid_{ 0 };
  gid_t realGid_{ 0 };

  PrivilegeManager();

public:
  static PrivilegeManager&
  Instance()
  {
    static PrivilegeManager instance;

    return instance;
  }

  PrivilegeManager(const PrivilegeManager&) = delete;
  PrivilegeManager&
  operator = (const PrivilegeManager&) = delete;
  PrivilegeManager(PrivilegeManager&&) = delete;
  PrivilegeManager&
  operator = (PrivilegeManager&&) = delete;

  /**
   * @brief Evaluates if the process has the required permissions for the requested port,
   * and permanently drops unused root capabilities if the port is unprivileged.
   * @param port The port number the process intends to bind to (0 for clients/child utilities).
   */
  void
  EnforcePortPolicy(int port);

  /**
   * @brief Temporarily drops effective root privileges, reverting to the real user.
   * @return true if successful or if swapping wasn't required.
   */
  bool
  LowerPrivileges();

  /**
   * @brief Restores effective root privileges if the process is privileged.
   * @return true if successful or if swapping wasn't required.
   */
  bool
  RaisePrivileges();

  /**
   * @brief Permanently revokes root capabilities for this process (and its future children).
   * @return true if successful.
   */
  bool
  PermanentlyDropPrivileges();
};
}
