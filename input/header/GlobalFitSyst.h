#pragma once
// GlobalFitSyst (was globalFitSyst.C): systematic shapes for the global fit, written into
// the pre-fit file (UPDATE):
//   <etaDir>/sys/  multijet JER, photon scale (Zee mass eigenvectors, gain paths),
//                  hadronic-W fitProb and FSR shapes;
//   <etaDir>/fsr/  hadw_fsr;
//   <etaDir>/      the HDM variants hdm_gamjet, ghdm_hadw/hdm_hadw, ghdm_incjet/hdm_incjet.
// Settings: input/config/GlobalFitSyst.json; each block can be switched off.
#include <map>
#include <string>
#include <vector>

#include "InputStep.h"
#include "TdrStyle.h"

class GlobalFitSyst : public InputStep {
 public:
  struct Function {
    std::string formula;
    double xmin, xmax;
    std::vector<double> parameters;
  };
  struct MultijetJer {
    bool enabled;
    std::string mode, binning, clone, output;
    double ptMin, ptMax, scale;
    std::map<std::string, Function> functions;  // "leading", "recoil"
  };
  struct GamMass {
    bool enabled;
    std::string binning, formula, output;
    double xmin, xmax, ptFactor;
    std::vector<std::vector<double>> errorMatrix;
  };
  struct GamGains {
    bool enabled;
    std::string file, gain1Profile, gain1Output, gain6Profile, gain6Output;
    double scale;
    std::string hdmSource, hdmOutput;
    double gain1Weight, gain6Weight;
  };
  struct HadwFitProb {
    bool enabled;
    std::string binning;
    double scale;
    std::map<std::string, Function> functions;
    struct Output {
      std::string name, function, minus;  // minus: empty if none
    };
    std::vector<Output> outputs;
  };
  struct HadwFsr {
    bool enabled;
    std::string file, nominal, up, down, fsr;
    double fsrScale;
    std::string graph, graphClone, graphOutput, histOutput;
    std::vector<std::string> sysOutputs;
  };
  struct IncjetHdm {
    bool enabled;
    std::string referenceFile, reference, graph, graphOutput, histOutput;
  };
  struct Settings {
    std::string etaDir, fsrDir, sysDir;
    MultijetJer multijetJer;
    GamMass gamMass;
    GamGains gamGains;
    HadwFitProb hadwFitProb;
    HadwFsr hadwFsr;
    IncjetHdm incjetHdm;

    // `inputs` resolves the "{prefit}/..." paths.
    static Settings parse(const nlohmann::json& json, const std::string& where, const Inputs& inputs);
  };

  GlobalFitSyst(const Inputs& inputs, const Epoch& epoch);

  std::string name() const override { return "globalFitSyst"; }
  void run(const std::string& jecdataPath) override;

 private:
  Epoch epoch_;
  Settings settings_;
  TdrStyle style_;
};
