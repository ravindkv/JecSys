#pragma once
// Minimal test helpers: CHECK counts failures, the test's main() returns testTools::finish().
#include <cmath>
#include <functional>
#include <iostream>
#include <string>

#include "JecSysError.h"

namespace testTools {

inline int& failures() {
  static int count = 0;
  return count;
}

inline void check(bool ok, const char* expression, const char* file, int line) {
  if (ok) return;
  ++failures();
  std::cout << "  FAILED " << file << ":" << line << ": " << expression << "\n";
}

// True if `body` throws a JecSysError whose message contains `fragment`.
inline bool throwsError(const std::function<void()>& body, const std::string& fragment = "") {
  try {
    body();
  } catch (const JecSysError& error) {
    return std::string(error.what()).find(fragment) != std::string::npos;
  }
  return false;
}

inline bool close(double a, double b, double tolerance = 1e-12) {
  return std::fabs(a - b) <= tolerance * std::max(1., std::max(std::fabs(a), std::fabs(b)));
}

inline int finish(const char* name) {
  std::cout << "  " << name << ": " << (failures() == 0 ? "OK" : "FAILED") << "\n";
  return failures() == 0 ? 0 : 1;
}

}  // namespace testTools

#define CHECK(expression) testTools::check((expression), #expression, __FILE__, __LINE__)
