#include "ConfigFile.h"

#include <fstream>

#include "JecSysError.h"

namespace {

// Remove "_note"-style keys at every level.
void stripNotes(nlohmann::json& node) {
  if (node.is_object()) {
    for (auto it = node.begin(); it != node.end();) {
      if (!it.key().empty() && it.key()[0] == '_') {
        it = node.erase(it);
      } else {
        stripNotes(it.value());
        ++it;
      }
    }
  } else if (node.is_array()) {
    for (auto& element : node) stripNotes(element);
  }
}

// Every key of an era or epoch override must exist in "default" (at every level where
// both are objects), so a typo in an override is an error and not a silent new key.
void checkOverride(const nlohmann::json& patch, const nlohmann::json& reference, const std::string& where) {
  if (!patch.is_object()) throw JecSysError(where + ": expected a JSON object");
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (!it.key().empty() && it.key()[0] == '_') continue;
    if (!reference.contains(it.key())) {
      throw JecSysError(where + ": unknown key '" + it.key() + "' (not in \"default\")");
    }
    const nlohmann::json& base = reference[it.key()];
    if (it.value().is_object() && base.is_object()) checkOverride(it.value(), base, where + "." + it.key());
  }
}

}  // namespace

ConfigFile::ConfigFile(const std::string& path) : path_(path) {
  std::ifstream in(path);
  if (!in) throw JecSysError("Cannot open configuration file '" + path + "'");
  try {
    json_ = nlohmann::json::parse(in);
  } catch (const nlohmann::json::exception& error) {
    throw JecSysError("Cannot parse " + path + ": " + error.what());
  }
}

nlohmann::json ConfigFile::resolve(const std::string& era, const std::string& epoch) const {
  checkKeys(json_, {"default", "eras", "epochs"}, path_);
  nlohmann::json result = json_.value("default", nlohmann::json::object());
  for (const char* block : {"eras", "epochs"}) {
    if (!json_.contains(block)) continue;
    const nlohmann::json& overrides = json_[block];
    if (!overrides.is_object()) throw JecSysError(path_ + ": " + block + ": expected a JSON object");
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
      if (!it.key().empty() && it.key()[0] == '_') continue;
      checkOverride(it.value(), result, path_ + ": " + block + "." + it.key());
    }
  }
  if (json_.contains("eras") && json_["eras"].contains(era)) result.merge_patch(json_["eras"][era]);
  if (json_.contains("epochs") && json_["epochs"].contains(epoch)) {
    result.merge_patch(json_["epochs"][epoch]);
  }
  stripNotes(result);
  return result;
}

void ConfigFile::checkKeys(const nlohmann::json& object, const std::set<std::string>& allowed,
                           const std::string& where) {
  if (!object.is_object()) throw JecSysError(where + ": expected a JSON object");
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (!it.key().empty() && it.key()[0] == '_') continue;
    if (!allowed.count(it.key())) throw JecSysError(where + ": unknown key '" + it.key() + "'");
  }
}

std::runtime_error ConfigFile::configError(const std::string& message) {
  return JecSysError(message);
}
