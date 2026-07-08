#pragma once
#include "Log.h"
#include "PrivilegeManager.h"
#include "SignalManager.h"

#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <iostream>

#include <libgen.h>
#include <unistd.h>
#include <getopt.h>
#include <spdlog/fmt/bundled/core.h>

namespace rdm {
struct CmdOption {
  char        shortOpt;
  std::string longName;
  std::string description;
  bool        requiresArg;
  std::string value;
  bool        isSet{ false };
};

class Application {
private:
  std::map<char, CmdOption> options_;
  std::string optString_;

protected:
  std::string progname_;
  std::string appDescription_;
  std::vector<std::string> positionalArgs_;

  Application(std::string description = "") : appDescription_(std::move(description))
  {
    RegisterFlag('v', "Verbose: log INFO-level messages");
    RegisterFlag('x', "Debug: log DEBUG-level messages");
    RegisterOption('l', "log", "Log destination (default: system logging daemon)", "");
  }

  void
  RegisterFlag(char shortOpt, const std::string& desc)
  {
    options_[shortOpt] = { shortOpt, "", desc, false, "", false };
    optString_        += shortOpt;
  }

  void
  RegisterOption(char shortOpt, const std::string& longName, const std::string& desc,
    const std::string& defaultVal = "")
  {
    options_[shortOpt] = { shortOpt, longName, desc, true, defaultVal, false };
    optString_        += shortOpt;
    optString_        += ":";
  }

  bool
  IsSet(char shortOpt) const
  {
    auto it = options_.find(shortOpt);

    return it != options_.end() && it->second.isSet;
  }

  std::string
  GetOption(char shortOpt) const
  {
    auto it = options_.find(shortOpt);

    return it != options_.end() ? it->second.value : "";
  }

  virtual void ConfigureOptions(){ }

  virtual bool ProcessOptions(){ return true; }

  virtual bool Initialize(){ return true; }

  virtual int
  Run() = 0;

  virtual void
  DropPrivileges()
  {
    PrivilegeManager::Instance().PermanentlyDropPrivileges();
  }

  void
  PrintUsage() const
  {
    fmt::print(stderr, "Usage: {} [options] [args...]\n", progname_);
    if (!appDescription_.empty()) {
      fmt::print(stderr, "{}\n", appDescription_);
    }
    fmt::print(stderr, "Options:\n");
    for (const auto& [ch, opt] : options_) {
      if (opt.requiresArg) {
        fmt::print(stderr, "\t-{} {:<10} {}\n", ch, opt.longName, opt.description);
      } else {
        fmt::print(stderr, "\t-{} {:<10} {}\n", ch, "", opt.description);
      }
    }
  }

  bool
  ParseArguments(int argc, char * argv[])
  {
    ConfigureOptions();

    int ch;
    ::opterr = 1;
    ::optind = 1;

    while ((ch = getopt(argc, argv, optString_.c_str())) != EOF) {
      if (ch == '?') { return false; }

      auto it = options_.find(ch);
      if (it != options_.end()) {
        it->second.isSet = true;
        if (it->second.requiresArg && ::optarg) {
          it->second.value = ::optarg;
        }
      }
    }

    for (int i = ::optind; i < argc; ++i) {
      positionalArgs_.push_back(argv[i]);
    }

    if (IsSet('v') && !log_is_enabled_info) { log_set_level(LOG_LEVEL_INFO); }
    if (IsSet('x')) { log_set_level(LOG_LEVEL_DEBUG); }
    if (IsSet('l') && !GetOption('l').empty()) {
      if (log_set_destination(GetOption('l').c_str())) {
        LogSyserr("Couldn't set logging destination to \"{}\"", GetOption('l'));
        return false;
      }
    }

    return ProcessOptions();
  } // ParseArguments

public:
  virtual
  ~Application() = default;

  int
  Execute(int argc, char * argv[])
  {
    DropPrivileges();

    char * argv0_copy = strdup(argv[0]);

    progname_ = basename(argv0_copy);
    free(argv0_copy);

    if (LogInitialize(progname_.c_str())) { return EXIT_FAILURE; }
    if (!ParseArguments(argc, argv)) {
      PrintUsage();
      return EXIT_FAILURE;
    }

    SignalManager::Initialize();

    if (!Initialize()) { return EXIT_FAILURE; }

    int status = Run();

    // Spams: LogNotice("Exiting");
    LogShutdown();
    return status;
  }
};
}
