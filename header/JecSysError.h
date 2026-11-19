#pragma once
// Exception thrown for every run-time problem (missing file, bad card, ...).
// The executables catch it in main(), print the message and exit with code 1.
#include <stdexcept>
#include <string>

class JecSysError : public std::runtime_error {
 public:
  explicit JecSysError(const std::string& message) : std::runtime_error(message) {}
};
