#pragma once
#include <string>
#include <vector>

namespace rdm {
class Wordexp {
private:
  std::vector<std::string> tokens_;
  std::vector<char *> argv_; // Pointers into tokens_ for execvp()

  void
  buildArgv();

public:
  // Throws std::invalid_argument if quotes are mismatched
  explicit
  Wordexp(const std::string& words);
  Wordexp() = default;

  // Copy semantics
  Wordexp(const Wordexp& other);
  Wordexp&
  operator = (const Wordexp& other);

  // Move semantics
  Wordexp(Wordexp&& other) noexcept;
  Wordexp&
  operator = (Wordexp&& other) noexcept;

  // Returns a null-terminated array of char pointers suitable for POSIX execvp()
  char * const *
  getArgv() const { return const_cast<char * const *>(argv_.data()); }

  size_t
  getArgc() const { return tokens_.size(); }

  const std::vector<std::string>&
  getTokens() const { return tokens_; }
};
}
