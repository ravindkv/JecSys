#include "Inputs.h"

#include <algorithm>
#include <set>

#include "ConfigFile.h"
#include "JecSysError.h"
#include "TextTools.h"

namespace {
const std::set<std::string> kRoots = {"card", "postfit", "prefit", "forfit", "protolab"};
}

Inputs::Inputs(const std::string& path) : path_(path) {
  const ConfigFile file(path);
  // nlohmann::ordered_json would keep the order of the file; a plain json sorts the
  // keys, which for our epoch names is the chronological order we want in `-h`.
  const nlohmann::json& json = file.raw();
  ConfigFile::checkKeys(json, {"roots", "epochs"}, path);
  const auto roots = ConfigFile::get<nlohmann::json>(json, "roots", path);
  ConfigFile::checkKeys(roots, kRoots, path + ": roots");
  for (const std::string& name : kRoots) {
    roots_[name] = ConfigFile::get<std::string>(roots, name, path + ": roots");
  }

  const auto epochs = ConfigFile::get<nlohmann::json>(json, "epochs", path);
  for (auto it = epochs.begin(); it != epochs.end(); ++it) {
    const std::string where = path + ": epochs." + it.key();
    ConfigFile::checkKeys(it.value(), {"era", "refIov", "corLevel", "forfit", "ptBinning"}, where);
    Epoch epoch;
    epoch.name = it.key();
    epoch.era = ConfigFile::get<std::string>(it.value(), "era", where);
    epoch.refIov = ConfigFile::get<std::string>(it.value(), "refIov", where);
    epoch.corLevel = ConfigFile::get<std::string>(it.value(), "corLevel", where);
    epoch.forfit = resolvePath(ConfigFile::get<std::string>(it.value(), "forfit", where));
    const auto binning = ConfigFile::get<nlohmann::json>(it.value(), "ptBinning", where);
    ConfigFile::checkKeys(binning, {"file", "object"}, where + ".ptBinning");
    epoch.ptBinning.file = resolvePath(ConfigFile::get<std::string>(binning, "file", where + ".ptBinning"));
    epoch.ptBinning.object = ConfigFile::get<std::string>(binning, "object", where + ".ptBinning");
    if (epoch.corLevel != "L1L2L3Res" && epoch.corLevel != "L1L2Res") {
      throw JecSysError(where + ": corLevel must be L1L2L3Res or L1L2Res");
    }
    epochs_[epoch.name] = epoch;
    order_.push_back(epoch.name);
  }
  for (const auto& [name, epoch] : epochs_) {
    if (!epochs_.count(epoch.refIov)) {
      throw JecSysError(path + ": epoch " + name + " has unknown refIov '" + epoch.refIov + "'");
    }
    if (!epochs_.at(epoch.refIov).isRefIov()) {
      throw JecSysError(path + ": refIov of " + name + " (" + epoch.refIov +
                        ") is not itself a reference IOV");
    }
  }
}

const Epoch& Inputs::epoch(const std::string& name) const {
  const auto it = epochs_.find(name);
  if (it == epochs_.end()) {
    throw JecSysError("Unknown epoch '" + name + "' (known: " + textTools::join(order_, ", ") + ")");
  }
  return it->second;
}

std::vector<std::string> Inputs::epochsInDependencyOrder() const {
  std::vector<std::string> result;
  for (const std::string& name : order_) {
    if (epochs_.at(name).isRefIov()) result.push_back(name);
  }
  for (const std::string& name : order_) {
    if (!epochs_.at(name).isRefIov()) result.push_back(name);
  }
  return result;
}

const std::string& Inputs::root(const std::string& name) const {
  const auto it = roots_.find(name);
  if (it == roots_.end()) throw JecSysError(path_ + ": no root '" + name + "'");
  return it->second;
}

std::string Inputs::resolvePath(const std::string& path) const {
  textTools::TemplateValues values;
  for (const auto& [name, dir] : roots_) values.emplace(name, dir);
  try {
    return textTools::formatTemplate(path, values);
  } catch (const JecSysError& error) {
    throw JecSysError("path '" + path + "' (roots from " + path_ + "): " + error.what());
  }
}

std::string Inputs::cardDir(const Epoch& epoch) const {
  return root("card") + "/" + epoch.corLevel + "/" + epoch.name;
}

std::string Inputs::postfitDir(const Epoch& epoch) const {
  return root("postfit") + "/" + epoch.corLevel + "/" + epoch.name;
}
