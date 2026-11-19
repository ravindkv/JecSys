#pragma once
// SoftRad (was softrad3.C): FSR corrections with the HT decomposition method (HDM) for
// one eta range. For every sample, method (MPF, pT balance) and tier (ratio, data, mc) it
// solves the HDM model for the response R1 of the leading jet in data and MC, bin by bin
// in pT, and writes into the pre-fit file (UPDATE):
//   <tier>/eta<..>/hdm_<method>_<sample>                  HDM response
//   <tier>/eta<..>/fsr/hkfsr3_<method>_<sample>[<comp>]   FSR correction and its components
// Settings: input/config/SoftRad.json.
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "InputStep.h"
#include "TdrStyle.h"

class SoftRad : public InputStep {
 public:
  struct ModelParameters {
    double rn, ru, r2;  // responses of the subleading jets, the unclustered energy, the recoil
  };
  struct Function {
    std::string name, formula;
    double xmin, xmax;
  };
  struct Settings {
    std::vector<std::string> readDirs, readMethods, writeDirs, methods;
    std::string flavors;
    std::map<std::string, std::vector<std::string>> samples;  // eta directory -> sample patterns
    int alphaRefDefault;
    std::map<std::string, int> alphaRef;
    std::map<std::string, std::string> countsFrom;
    ModelParameters modelDefault, modelMultijet;
    double dRnd, dRnm, dRud, dRum;  // systematic variations of Rn and Ru
    double k;
    double rootMin, rootMax;
    Function mpf, ptbal, mpfMultijet, ptbalMultijet;
    double multijetMpf, zjetMpf, zjetMpfWarning, ptbalWarning, sumRuleWarning;
    std::string etaDirName, inputName, fsrDirName, fsrName, hdmDirName, hdmName;
    std::string hdmClone;
    std::vector<std::pair<std::string, std::string>> fsrComponents;  // clone name, key suffix

    static Settings parse(const nlohmann::json& json, const std::string& where);
  };

  // `stepArguments`: {"etamin": .., "etamax": ..} of input/config/InputChain.json.
  SoftRad(const Epoch& epoch, const nlohmann::json& stepArguments, bool verbose);

  std::string name() const override;
  void run(const std::string& jecdataPath) override;

  // 'x*' -> x + one flavour letter, 'x**' -> x + tag + generator letter (MC only).
  static std::vector<std::string> expandSamples(const std::vector<std::string>& patterns,
                                                const std::string& flavors, std::set<std::string>& mcOnly);

 private:
  Epoch epoch_;
  double etamin_, etamax_;
  bool verbose_;
  Settings settings_;
  TdrStyle style_;
};
