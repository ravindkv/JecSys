#include "InputStep.h"

#include <map>

#include "ConfigFile.h"
#include "JecSysError.h"
#include "TDirectory.h"
#include "TF1.h"
#include "TAttMarker.h"
#include "TFile.h"
#include "Rtypes.h"
#include "TextTools.h"

std::pair<double, double> SettingsReader::range(const std::string& key) const {
  const auto values = get<std::vector<double>>(key);
  if (values.size() != 2) throw JecSysError(where_ + ": '" + key + "' must be [min, max]");
  return {values[0], values[1]};
}

namespace inputTools {

std::string configPath(const std::string& className) { return "input/config/" + className + ".json"; }

nlohmann::json loadSettings(const std::string& className, const Epoch& epoch) {
  return ConfigFile(configPath(className)).resolve(epoch.era, epoch.name);
}

std::string settingsWhere(const std::string& className, const Epoch& epoch) {
  return configPath(className) + " (" + epoch.name + ")";
}

std::unique_ptr<TFile> openFile(const std::string& path, const std::string& mode) {
  std::unique_ptr<TFile> file(TFile::Open(path.c_str(), mode.c_str()));
  if (!file || file->IsZombie()) throw JecSysError("Cannot open " + path + " (" + mode + ")");
  return file;
}

void requireFile(const std::string& path, const std::string& stepName) {
  if (!textTools::fileExists(path)) {
    throw JecSysError(stepName + ": pre-fit file " + path + " does not exist (run reprocess first)");
  }
}

TObject* get(TDirectory& dir, const std::string& name, const std::string& where) {
  TObject* object = dir.Get(name.c_str());
  if (!object) throw JecSysError(where + ": object '" + name + "' not found");
  return object;
}

void throwWrongType(const std::string& name, const std::string& where) {
  throw JecSysError(where + ": object '" + name + "' has an unexpected type");
}

int rootConstant(const std::string& name, const std::string& where) {
  static const std::map<std::string, int> constants = {
      {"kWhite", kWhite}, {"kBlack", kBlack}, {"kGray", kGray}, {"kRed", kRed}, {"kGreen", kGreen},
      {"kBlue", kBlue}, {"kYellow", kYellow}, {"kMagenta", kMagenta}, {"kCyan", kCyan}, {"kOrange", kOrange},
      {"kSpring", kSpring}, {"kTeal", kTeal}, {"kAzure", kAzure}, {"kViolet", kViolet}, {"kPink", kPink},
      {"kDot", kDot}, {"kPlus", kPlus}, {"kStar", kStar}, {"kCircle", kCircle}, {"kMultiply", kMultiply},
      {"kFullCircle", kFullCircle}, {"kFullSquare", kFullSquare}, {"kFullTriangleUp", kFullTriangleUp},
      {"kFullTriangleDown", kFullTriangleDown}, {"kOpenCircle", kOpenCircle}, {"kOpenSquare", kOpenSquare},
      {"kOpenTriangleUp", kOpenTriangleUp}, {"kOpenDiamond", kOpenDiamond}, {"kOpenCross", kOpenCross},
      {"kFullStar", kFullStar}, {"kOpenStar", kOpenStar}, {"kOpenTriangleDown", kOpenTriangleDown},
      {"kFullDiamond", kFullDiamond}, {"kFullCross", kFullCross}};
  const std::size_t plus = name.find('+');
  const std::string base = textTools::replaceAll(name.substr(0, plus), " ", "");
  const auto it = constants.find(base);
  if (it == constants.end()) throw JecSysError(where + ": unknown ROOT constant '" + name + "'");
  return it->second + (plus == std::string::npos ? 0 : textTools::toInt(name.substr(plus + 1), where + ": " + name));
}

std::unique_ptr<TF1> makeFunction(const std::string& name, const std::string& formula, double xmin,
                                  double xmax, const std::string& where) {
  auto function = std::make_unique<TF1>(name.c_str(), formula.c_str(), xmin, xmax);
  if (!function->GetFormula() || !function->GetFormula()->IsValid()) {
    throw JecSysError(where + ": cannot parse the formula '" + formula + "' of " + name);
  }
  return function;
}

}  // namespace inputTools
