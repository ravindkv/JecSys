#pragma once
// One step of the input-creation chain (InputChain, input/config/InputChain.json): it
// creates or updates the pre-fit file jecdata<epoch>.root. The steps communicate only
// through that file. Each step reads its settings from input/config/<Class>.json in its
// constructor, so bad settings are reported before any ROOT file is opened.
//
// ROOT state: the steps reproduce the ROOT macros byte for byte, including where an
// object is attached (gDirectory) and which objects a TFile::Write() writes again. They
// therefore use TFile/TDirectory directly, not InputFile/OutputFile, and need ROOT's
// default TH1::AddDirectory(true), which InputChain::run sets (runMain() of the shared
// CommandLine turns it off for the fit stage). Every step starts and ends with gROOT as
// the current directory.
#include <memory>
#include <set>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "ConfigFile.h"
#include "Inputs.h"

class TDirectory;
class TF1;
class TFile;
class TObject;

class InputStep {
 public:
  virtual ~InputStep() = default;
  // Name for messages and timing, e.g. "softRad_13".
  virtual std::string name() const = 0;
  // Creates or updates the pre-fit file at `jecdataPath`.
  virtual void run(const std::string& jecdataPath) = 0;
};

// Typed access to one JSON object of a configuration. The constructor rejects keys that
// are not in `keys`; every error names `where` (file, epoch and path of the object).
class SettingsReader {
 public:
  SettingsReader(const nlohmann::json& object, std::string where, const std::set<std::string>& keys)
      : object_(object), where_(std::move(where)) {
    ConfigFile::checkKeys(object_, keys, where_);
  }
  template <class T>
  T get(const std::string& key) const {
    return ConfigFile::get<T>(object_, key, where_);
  }
  // The sub-object `key`, with its own allowed keys.
  SettingsReader child(const std::string& key, const std::set<std::string>& keys) const {
    return SettingsReader(ConfigFile::get<nlohmann::json>(object_, key, where_), where_ + "." + key, keys);
  }
  // A [min, max] pair.
  std::pair<double, double> range(const std::string& key) const;
  bool has(const std::string& key) const { return object_.contains(key); }
  const nlohmann::json& json() const { return object_; }
  const std::string& where() const { return where_; }

 private:
  nlohmann::json object_;
  std::string where_;
};

namespace inputTools {

// input/config/<className>.json, relative to the repository root.
std::string configPath(const std::string& className);

// The settings of input/config/<className>.json for `epoch` (default <- era <- epoch).
nlohmann::json loadSettings(const std::string& className, const Epoch& epoch);
// "input/config/<className>.json (<epoch>)", for error messages.
std::string settingsWhere(const std::string& className, const Epoch& epoch);

// TFile::Open that throws JecSysError instead of returning a null or zombie file.
// As TFile::Open, it makes the new file the current directory.
std::unique_ptr<TFile> openFile(const std::string& path, const std::string& mode = "READ");

// Throws if the pre-fit file does not exist: UPDATE mode would create an empty file.
void requireFile(const std::string& path, const std::string& stepName);

// dir.Get(name); throws JecSysError (naming `where`) if the object is missing or not a T.
// A histogram stays attached to its directory (owned by the file); other objects (graphs)
// belong to the caller.
TObject* get(TDirectory& dir, const std::string& name, const std::string& where);
[[noreturn]] void throwWrongType(const std::string& name, const std::string& where);
template <class T>
T* get(TDirectory& dir, const std::string& name, const std::string& where) {
  TObject* object = get(dir, name, where);
  auto* typed = dynamic_cast<T*>(object);
  if (!typed) throwWrongType(name, where);
  return typed;
}

// The value of a ROOT colour or marker constant, with an optional offset: "kRed+1",
// "kFullCircle". Throws JecSysError (naming `where`) for an unknown name.
int rootConstant(const std::string& name, const std::string& where);

// new TF1(name, formula, xmin, xmax), with an error if ROOT cannot parse the formula.
std::unique_ptr<TF1> makeFunction(const std::string& name, const std::string& formula, double xmin,
                                  double xmax, const std::string& where);

}  // namespace inputTools
