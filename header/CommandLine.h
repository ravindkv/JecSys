#pragma once
// Minimal command-line parser shared by the executables, plus the common main()
// wrapper that turns a JecSysError into a message and exit code 1.
#include <functional>
#include <map>
#include <string>
#include <vector>

class CommandLine {
 public:
  CommandLine(std::string program, std::string usage, std::string description);

  // An option "--longName" (and "-s" if shortName is not 0). If valueName is empty the
  // option is a flag; otherwise it takes one value.
  void addOption(const std::string& longName, char shortName, const std::string& valueName,
                 const std::string& help);
  void setFooter(const std::string& footer) { footer_ = footer; }

  // Throws JecSysError on an unknown option or a missing value.
  void parse(int argc, char** argv);

  bool helpRequested() const { return has("help"); }
  bool has(const std::string& longName) const { return values_.count(longName) != 0; }
  std::string value(const std::string& longName, const std::string& fallback = "") const;
  const std::vector<std::string>& positional() const { return positional_; }
  std::string help() const;

 private:
  struct Option {
    std::string longName;
    char shortName;
    std::string valueName;
    std::string help;
  };
  const Option* find(const std::string& argument) const;
  std::string program_, usage_, description_, footer_;
  std::vector<Option> options_;
  std::map<std::string, std::string> values_;
  std::vector<std::string> positional_;
};

// Runs `body`, prints "<program>: error: ..." for a JecSysError and returns the exit code.
int runMain(const std::string& program, const std::function<int()>& body);
