#include "JesModel9.h"

#include <algorithm>

#include "ConfigFile.h"
#include "JecSysError.h"

namespace {

ShapeFunction readShape(const nlohmann::json& node, const std::string& where) {
  ConfigFile::checkKeys(node, {"form", "par"}, where);
  return ShapeFunction(ConfigFile::get<std::string>(node, "form", where),
                       ConfigFile::get<std::vector<double>>(node, "par", where), where);
}

}  // namespace

JesModel9::JesModel9(const nlohmann::json& config) {
  const std::string where = "JesModel9";
  ConfigFile::checkKeys(config,
                        {"parameters", "fragWeight", "trkDataMcWeight", "responseShapes", "pfShapes"},
                        where);

  static const std::array<const char*, kNPar> expected = {"p0Trk", "p1Gam", "p2HadX", "p3HadH", "p4HadE",
                                                          "p5Frag", "p6L1RC", "p7TrkD", "p8Flv"};
  const auto parameters = ConfigFile::get<nlohmann::json>(config, "parameters", where);
  if (!parameters.is_array() || parameters.size() != kNPar) {
    throw JecSysError(where + ": 'parameters' must list exactly 9 parameters");
  }
  for (int i = 0; i != kNPar; ++i) {
    const std::string sub = where + ": parameters[" + std::to_string(i) + "]";
    ConfigFile::checkKeys(parameters[i], {"name", "start", "step", "range", "penalized"}, sub);
    FitParameter p;
    p.name = ConfigFile::get<std::string>(parameters[i], "name", sub);
    if (p.name != expected[i]) {
      throw JecSysError(sub + ": expected '" + expected[i] + "' (the order is part of the model)");
    }
    p.start = ConfigFile::get<double>(parameters[i], "start", sub);
    p.step = ConfigFile::get<double>(parameters[i], "step", sub);
    const auto range = ConfigFile::get<std::vector<double>>(parameters[i], "range", sub);
    if (range.size() != 2) throw JecSysError(sub + ": range must be [min, max]");
    p.lower = range[0];
    p.upper = range[1];
    p.penalized = ConfigFile::get<bool>(parameters[i], "penalized", sub);
    parameters_.push_back(p);
  }

  fragWeight_ = ConfigFile::get<double>(config, "fragWeight", where);
  trkDataMcWeight_ = ConfigFile::get<double>(config, "trkDataMcWeight", where);

  const auto shapes = ConfigFile::get<nlohmann::json>(config, "responseShapes", where);
  ConfigFile::checkKeys(shapes, {"ftd", "ftm", "fp", "fhx", "fhh", "feh", "fhw", "fl1", "f1q3"},
                        where + ": responseShapes");
  const auto shape = [&](const char* key) {
    const std::string sub = where + ": responseShapes." + key;
    if (!shapes.contains(key)) throw JecSysError(sub + " is missing");
    return readShape(shapes[key], sub);
  };
  ftd_ = shape("ftd");
  ftm_ = shape("ftm");
  fp_ = shape("fp");
  fhx_ = shape("fhx");
  fhh_ = shape("fhh");
  feh_ = shape("feh");
  fhw_ = shape("fhw");
  fl1_ = shape("fl1");
  f1q3_ = shape("f1q3");

  const auto pf = ConfigFile::get<nlohmann::json>(config, "pfShapes", where);
  for (auto it = pf.begin(); it != pf.end(); ++it) {
    const std::string sub = where + ": pfShapes." + it.key();
    if (!it.value().is_array() || it.value().size() != kNPar) {
      throw JecSysError(sub + " must have 9 entries (one per parameter, null if none)");
    }
    std::array<std::optional<ShapeFunction>, kNPar> row;
    for (int i = 0; i != kNPar; ++i) {
      if (!it.value()[i].is_null()) row[i] = readShape(it.value()[i], sub);
    }
    fractionNames_.push_back(it.key());
    pfShapes_.push_back(row);
  }
}

double JesModel9::response(double pt, const double* p) const {
  // Same terms in the same order as jesFit() in globalFitL3Res.C:333-345.
  const double ftd = ftd_(pt);
  return (1 + p[0] * 0.01 * ftd                                    // tracker efficiency (data)
          + p[1] * 0.01 * fp_(pt)                                  // photon scale
          + p[2] * 0.01 * fhx_(pt)                                 // hadron cross-over
          + p[3] * 0.01 * fhh_(pt)                                 // hadrons in HCAL
          + p[4] * 0.01 * feh_(pt)                                 // hadrons in ECAL
          + fragWeight_ * p[5] * 0.01 * fhw_(pt)                   // Herwig fragmentation
          + p[6] * (fl1_(pt) - 1)                                  // L1RC - L1Simple
          + p[7] * 0.01 * trkDataMcWeight_ * (ftd - ftm_(pt))      // tracker data - MC
          + p[8] * (1 - f1q3_(pt)));                               // Z+q vs Z+jet
}

int JesModel9::fractionIndex(const std::string& fraction) const {
  const auto it = std::find(fractionNames_.begin(), fractionNames_.end(), fraction);
  if (it == fractionNames_.end()) {
    throw JecSysError("JesModel9 has no PF fraction '" + fraction + "'");
  }
  return static_cast<int>(it - fractionNames_.begin());
}

double JesModel9::pfFraction(int fractionIndex, double pt, const double* par) const {
  double df = 0;
  const auto& row = pfShapes_[fractionIndex];
  for (int i = 0; i != kNPar; ++i) {
    if (row[i]) df += 0.01 * (*row[i])(pt) * par[i];
  }
  return df;
}

// ---------------------------------------------------------------------------------------
// Model registry
// ---------------------------------------------------------------------------------------
std::vector<std::string> physicsModelNames() { return {"JesModel9"}; }

std::unique_ptr<PhysicsModel> makePhysicsModel(const std::string& name, const std::string& configText) {
  nlohmann::json config;
  try {
    config = nlohmann::json::parse(configText);
  } catch (const nlohmann::json::exception& error) {
    throw JecSysError("Cannot parse the configuration of model " + name + ": " + error.what());
  }
  if (name == "JesModel9") return std::make_unique<JesModel9>(config);
  throw JecSysError("Unknown physics model '" + name + "' (known: JesModel9)");
}
