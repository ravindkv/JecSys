#pragma once
// Small string helpers shared by the datacard, the workspace and the command lines.
#include <map>
#include <string>
#include <vector>

namespace textTools {

// A value for formatTemplate: text, an integer or a floating-point number.
class TemplateValue {
 public:
  enum class Kind { Text, Integer, Real };
  TemplateValue(const char* text) : kind_(Kind::Text), text_(text) {}
  TemplateValue(std::string text) : kind_(Kind::Text), text_(std::move(text)) {}
  TemplateValue(int value) : kind_(Kind::Integer), integer_(value) {}
  TemplateValue(long value) : kind_(Kind::Integer), integer_(value) {}
  TemplateValue(long long value) : kind_(Kind::Integer), integer_(value) {}
  TemplateValue(double value) : kind_(Kind::Real), real_(value) {}

  Kind kind() const { return kind_; }
  const std::string& text() const { return text_; }
  long long integer() const { return integer_; }
  double real() const { return real_; }

 private:
  Kind kind_;
  std::string text_;
  long long integer_ = 0;
  double real_ = 0;
};
using TemplateValues = std::map<std::string, TemplateValue>;

// The name templates of the configuration files, with Python str.format semantics
// (the configs were written for the Python port):
//   "{key}"        the value: text as is, an integer in decimal, a real as Python's
//                  repr (1.0 -> "1.0", 0.1 -> "0.1");
//   "{key:spec}"   a number formatted with printf("%" spec), for the specs on which
//                  Python and printf agree: [+ ][#][0][width][.precision] and a type
//                  d, x, X, o (integers only), f, F, e, E, g, G (integers or reals);
//   "{{", "}}"     a literal brace.
// Throws JecSysError for an unknown key, an unsupported spec, a spec on text, or a
// type that does not fit the value (e.g. "d" on a real, as Python does).
std::string formatTemplate(const std::string& pattern, const TemplateValues& values);

// Python's repr() of a float: the shortest text that reads back as the same number,
// with ".0" for integral values and an exponent below 1e-4 and from 1e16 on.
std::string pythonRepr(double value);

// Split on white space; an empty or blank line gives an empty vector.
std::vector<std::string> splitWords(const std::string& line);

// Split on one separator character; empty fields are kept.
std::vector<std::string> split(const std::string& text, char separator);

std::string join(const std::vector<std::string>& words, const std::string& separator);

// Replace every occurrence of `from` by `to`.
std::string replaceAll(std::string text, const std::string& from, const std::string& to);

// Shell-style match where '*' stands for any run of characters (also none).
bool globMatch(const std::string& pattern, const std::string& text);

// True if `text` matches the regular expression `pattern` as a whole.
bool regexMatch(const std::string& pattern, const std::string& text);

// Parse a number; throws JecSysError naming `what` if `text` is not a number.
double toDouble(const std::string& text, const std::string& what);
int toInt(const std::string& text, const std::string& what);

// Shortest decimal text that reads back as exactly the same double.
std::string formatDouble(double value);

// Directory part of a path ("." if there is none), and path joining that keeps
// absolute paths and URLs (root://...) untouched.
std::string dirName(const std::string& path);
std::string resolvePath(const std::string& baseDir, const std::string& path);

// Path of `target` as seen from directory `fromDir` (both relative to the current
// directory or both absolute).
std::string relativePath(const std::string& target, const std::string& fromDir);

void makeDirs(const std::string& dir);
bool fileExists(const std::string& path);

}  // namespace textTools
