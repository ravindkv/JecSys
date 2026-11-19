#include "TextTools.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <sstream>

#include "JecSysError.h"

namespace textTools {

namespace {

// printf with a format built at run time.
template <class T>
std::string printfFormat(const std::string& format, T value) {
  const int size = std::snprintf(nullptr, 0, format.c_str(), value);
  std::string result(size, '\0');
  std::snprintf(result.data(), size + 1, format.c_str(), value);
  return result;
}

bool isIdentifier(const std::string& text) {
  static const std::regex identifier("[A-Za-z_][A-Za-z0-9_]*");
  return std::regex_match(text, identifier);
}

std::string knownKeys(const TemplateValues& values) {
  std::vector<std::string> keys;
  for (const auto& entry : values) keys.push_back(entry.first);
  return keys.empty() ? "none" : join(keys, ", ");
}

// One "{key:spec}" field. `where` names the template in error messages.
std::string formatField(const TemplateValue& value, const std::string& spec, const std::string& where) {
  using Kind = TemplateValue::Kind;
  if (spec.empty()) {
    if (value.kind() == Kind::Text) return value.text();
    if (value.kind() == Kind::Integer) return std::to_string(value.integer());
    return pythonRepr(value.real());
  }
  // Python's [sign][#][0][width][.precision]type, restricted to what printf does alike.
  static const std::regex specPattern(R"(([+ ])?(#)?(0)?([0-9]+)?(\.[0-9]+)?([dxXofFeEgG]))");
  std::smatch match;
  if (!std::regex_match(spec, match, specPattern)) {
    throw JecSysError(where + ": unsupported format spec ':" + spec +
                      "' (supported: [+ ][#][0][width][.precision] and d, x, X, o, f, F, e, E, g, G)");
  }
  if (value.kind() == Kind::Text) throw JecSysError(where + ": format spec ':" + spec + "' on a text value");
  const std::string sign = match[1], alternate = match[2], zero = match[3], width = match[4],
                    precision = match[5];
  const char type = spec.back();
  const bool integerType = std::string("dxXo").find(type) != std::string::npos;
  if (integerType) {
    if (value.kind() != Kind::Integer) {
      throw JecSysError(where + ": format spec ':" + spec + "' needs an integer, got a real number");
    }
    if (!precision.empty()) throw JecSysError(where + ": precision not allowed in integer spec ':" + spec + "'");
    // Python's '#' gives "0o17" and "0x0" where printf gives "017" and "0".
    if (!alternate.empty()) throw JecSysError(where + ": '#' not supported in integer spec ':" + spec + "'");
    return printfFormat("%" + sign + zero + width + "ll" + type, value.integer());
  }
  const double number = value.kind() == Kind::Integer ? static_cast<double>(value.integer()) : value.real();
  if (std::isfinite(number)) {
    return printfFormat("%" + sign + alternate + zero + width + precision + type, number);
  }
  // Python writes nan without a sign and pads nan and inf with zeros under '0';
  // glibc writes "-nan" and pads with spaces.
  const bool upper = std::isupper(static_cast<unsigned char>(type));
  std::string body = std::isnan(number) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
  const std::string signText = (std::isinf(number) && number < 0) ? "-" : sign;
  const std::size_t total = width.empty() ? 0 : std::stoul(width);
  const std::size_t used = signText.size() + body.size();
  const std::size_t padding = total > used ? total - used : 0;
  if (!zero.empty()) return signText + std::string(padding, '0') + body;
  return std::string(padding, ' ') + signText + body;
}

}  // namespace

std::string pythonRepr(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
  // Shortest round-trip digits and the exponent, e.g. "1.2345e+17" or "-5e-324".
  char buffer[64];
  const auto end = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific).ptr;
  const std::string text(buffer, end);
  const std::size_t ePos = text.find('e');
  std::string mantissa = text.substr(0, ePos);
  const int exponent = std::atoi(text.c_str() + ePos + 1);
  std::string sign;
  if (mantissa[0] == '-') {
    sign = "-";
    mantissa.erase(0, 1);
  }
  std::string digits;
  for (char c : mantissa) {
    if (c != '.') digits += c;
  }
  const int nDigits = static_cast<int>(digits.size());
  const int pointPos = exponent + 1;  // digits before the decimal point
  if (pointPos > -4 && pointPos <= 16) {
    if (pointPos <= 0) return sign + "0." + std::string(-pointPos, '0') + digits;
    if (pointPos >= nDigits) return sign + digits + std::string(pointPos - nDigits, '0') + ".0";
    return sign + digits.substr(0, pointPos) + "." + digits.substr(pointPos);
  }
  std::string result = sign + digits.substr(0, 1);
  if (nDigits > 1) result += "." + digits.substr(1);
  const int absExponent = std::abs(exponent);
  result += exponent < 0 ? "e-" : "e+";
  if (absExponent < 10) result += "0";
  return result + std::to_string(absExponent);
}

std::string formatTemplate(const std::string& pattern, const TemplateValues& values) {
  const std::string where = "name template '" + pattern + "'";
  std::string result;
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    const char c = pattern[i];
    if (c == '}') {
      if (i + 1 < pattern.size() && pattern[i + 1] == '}') {
        result += '}';
        ++i;
        continue;
      }
      throw JecSysError(where + ": single '}' (write '}}' for a literal brace)");
    }
    if (c != '{') {
      result += c;
      continue;
    }
    if (i + 1 < pattern.size() && pattern[i + 1] == '{') {
      result += '{';
      ++i;
      continue;
    }
    const std::size_t close = pattern.find('}', i + 1);
    if (close == std::string::npos) throw JecSysError(where + ": '{' without '}'");
    const std::string field = pattern.substr(i + 1, close - i - 1);
    if (field.find('{') != std::string::npos) throw JecSysError(where + ": nested '{' in a field");
    const std::size_t colon = field.find(':');
    const std::string key = field.substr(0, colon);
    const std::string spec = colon == std::string::npos ? "" : field.substr(colon + 1);
    if (!isIdentifier(key)) throw JecSysError(where + ": bad field name '" + key + "'");
    const auto it = values.find(key);
    if (it == values.end()) {
      throw JecSysError(where + ": unknown key '" + key + "' (known: " + knownKeys(values) + ")");
    }
    result += formatField(it->second, spec, where);
    i = close;
  }
  return result;
}

std::vector<std::string> splitWords(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> words;
  std::string word;
  while (stream >> word) words.push_back(word);
  return words;
}

std::vector<std::string> split(const std::string& text, char separator) {
  std::vector<std::string> fields;
  std::string field;
  std::istringstream stream(text);
  while (std::getline(stream, field, separator)) fields.push_back(field);
  if (!text.empty() && text.back() == separator) fields.emplace_back();
  return fields;
}

std::string join(const std::vector<std::string>& words, const std::string& separator) {
  std::string result;
  for (std::size_t i = 0; i != words.size(); ++i) {
    if (i != 0) result += separator;
    result += words[i];
  }
  return result;
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
  if (from.empty()) return text;
  for (std::size_t pos = text.find(from); pos != std::string::npos;
       pos = text.find(from, pos + to.size())) {
    text.replace(pos, from.size(), to);
  }
  return text;
}

bool globMatch(const std::string& pattern, const std::string& text) {
  // Iterative matcher with backtracking to the last '*'.
  std::size_t p = 0, t = 0, starP = std::string::npos, starT = 0;
  while (t < text.size()) {
    if (p < pattern.size() && pattern[p] == '*') {
      starP = p++;
      starT = t;
    } else if (p < pattern.size() && pattern[p] == text[t]) {
      ++p;
      ++t;
    } else if (starP != std::string::npos) {
      p = starP + 1;
      t = ++starT;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

bool regexMatch(const std::string& pattern, const std::string& text) {
  try {
    return std::regex_match(text, std::regex(pattern));
  } catch (const std::regex_error&) {
    throw JecSysError("Invalid regular expression '" + pattern + "'");
  }
}

double toDouble(const std::string& text, const std::string& what) {
  try {
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    if (used == text.size()) return value;
  } catch (const std::exception&) {
  }
  throw JecSysError("Expected a number for " + what + ", got '" + text + "'");
}

int toInt(const std::string& text, const std::string& what) {
  try {
    std::size_t used = 0;
    const int value = std::stoi(text, &used);
    if (used == text.size()) return value;
  } catch (const std::exception&) {
  }
  throw JecSysError("Expected an integer for " + what + ", got '" + text + "'");
}

std::string formatDouble(double value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

std::string dirName(const std::string& path) {
  const std::string dir = std::filesystem::path(path).parent_path().string();
  return dir.empty() ? "." : dir;
}

std::string resolvePath(const std::string& baseDir, const std::string& path) {
  if (path.empty() || path[0] == '/' || path.find("://") != std::string::npos) return path;
  return (std::filesystem::path(baseDir) / path).lexically_normal().string();
}

std::string relativePath(const std::string& target, const std::string& fromDir) {
  if (target.find("://") != std::string::npos) return target;
  const auto absTarget = std::filesystem::absolute(target).lexically_normal();
  const auto absFrom = std::filesystem::absolute(fromDir).lexically_normal();
  return absTarget.lexically_relative(absFrom).string();
}

void makeDirs(const std::string& dir) {
  std::error_code error;
  std::filesystem::create_directories(dir, error);
  if (error) throw JecSysError("Cannot create directory '" + dir + "': " + error.message());
}

bool fileExists(const std::string& path) { return std::filesystem::exists(path); }

}  // namespace textTools
