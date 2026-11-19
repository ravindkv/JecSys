#pragma once
// One JSON configuration file with the common layout
//   { "default": {...}, "eras": {"UL16": {...}}, "epochs": {"2016GH": {...}} }
// resolve() merges default <- era <- epoch (JSON merge patch), so every setting is
// written once. Every key of an era or epoch block must exist in "default" (checked for
// all blocks, at every level where both are objects), so a typo is an error. A null in
// "default" means "set per era/epoch". Keys that start with '_' are notes (_note,
// _source) and are ignored.
#include <set>
#include <string>

#include <nlohmann/json.hpp>

class ConfigFile {
 public:
  explicit ConfigFile(const std::string& path);

  const std::string& path() const { return path_; }
  const nlohmann::json& raw() const { return json_; }

  // Settings for one epoch, notes removed.
  nlohmann::json resolve(const std::string& era, const std::string& epoch) const;

  // Throws if `object` has a key that is not in `allowed`. `where` names the object
  // in the error message. Catches typos in configuration files.
  static void checkKeys(const nlohmann::json& object, const std::set<std::string>& allowed,
                        const std::string& where);

  // Typed access with a clear error if the key is missing or has the wrong type.
  template <class T>
  static T get(const nlohmann::json& object, const std::string& key, const std::string& where) {
    if (!object.is_object() || !object.contains(key)) {
      throw configError(where + ": missing key '" + key + "'");
    }
    try {
      return object.at(key).get<T>();
    } catch (const nlohmann::json::exception& error) {
      throw configError(where + ": key '" + key + "' has the wrong type (" + error.what() + ")");
    }
  }

 private:
  static std::runtime_error configError(const std::string& message);
  std::string path_;
  nlohmann::json json_;
};
