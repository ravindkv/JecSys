#include "CommandLine.h"

#include <iostream>
#include <sstream>

#include "JecSysError.h"
#include "TError.h"
#include "TH1.h"

CommandLine::CommandLine(std::string program, std::string usage, std::string description)
    : program_(std::move(program)), usage_(std::move(usage)), description_(std::move(description)) {
  addOption("help", 'h', "", "print this message");
}

void CommandLine::addOption(const std::string& longName, char shortName, const std::string& valueName,
                            const std::string& help) {
  options_.push_back({longName, shortName, valueName, help});
}

const CommandLine::Option* CommandLine::find(const std::string& argument) const {
  for (const Option& option : options_) {
    if (argument == "--" + option.longName) return &option;
    if (option.shortName != 0 && argument == std::string("-") + option.shortName) return &option;
  }
  return nullptr;
}

void CommandLine::parse(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    // A negative number is a value, not an option.
    const bool looksLikeOption =
        argument.size() > 1 && argument[0] == '-' && !(std::isdigit(argument[1]) || argument[1] == '.');
    if (!looksLikeOption) {
      positional_.push_back(argument);
      continue;
    }
    const Option* option = find(argument);
    if (!option) throw JecSysError("unknown option '" + argument + "' (see " + program_ + " -h)");
    if (option->valueName.empty()) {
      values_[option->longName] = "true";
    } else {
      if (i + 1 >= argc) throw JecSysError("option '" + argument + "' needs a value");
      values_[option->longName] = argv[++i];
    }
  }
}

std::string CommandLine::value(const std::string& longName, const std::string& fallback) const {
  const auto it = values_.find(longName);
  return it == values_.end() ? fallback : it->second;
}

std::string CommandLine::help() const {
  std::ostringstream out;
  out << program_ << " - " << description_ << "\n\nUsage: " << usage_ << "\n\nOptions:\n";
  for (const Option& option : options_) {
    std::string left = "  ";
    left += option.shortName != 0 ? std::string("-") + option.shortName + ", " : "    ";
    left += "--" + option.longName;
    if (!option.valueName.empty()) left += " <" + option.valueName + ">";
    if (left.size() < 34) left.resize(34, ' ');
    out << left << " " << option.help << "\n";
  }
  if (!footer_.empty()) out << "\n" << footer_ << "\n";
  return out.str();
}

int runMain(const std::string& program, const std::function<int()>& body) {
  TH1::AddDirectory(false);          // histograms are owned by our code, not by the current file
  gErrorIgnoreLevel = kWarning;      // no ROOT "Info" chatter
  try {
    return body();
  } catch (const JecSysError& error) {
    std::cerr << program << ": error: " << error.what() << "\n";
  } catch (const std::exception& error) {
    std::cerr << program << ": unexpected error: " << error.what() << "\n";
  }
  return 1;
}
