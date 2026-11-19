#pragma once
// GlobalFitRenormPF (was globalFitRenormPF.C): renormalised PF fractions and pre-combined
// PF and HDM inputs, written into the pre-fit file (UPDATE). Not used by the fit today.
//   1. <tier>/<eta>/<var>_<set>_ren: data and MC PF fractions (chf, nef, nhf) scaled to
//      add up to one, and their data-MC difference;
//   2. <tier>/<eta>/<var>_cmb_ren[_stat,_syst,_tot]: the fractions of all sets combined;
//   3. ratio/<eta>/hdm_cmb[_mj][_res][_syst,_tot]: the HDM responses combined.
// Each part opens and closes the file and ends with TFile::Write(), which writes the
// histograms read from the file again (new key cycles), as in the macro.
// Settings: input/config/GlobalFitRenormPF.json.
#include <map>
#include <string>
#include <vector>

#include "InputStep.h"
#include "TdrStyle.h"

class GlobalFitRenormPF : public InputStep {
 public:
  struct Variant {
    bool addMultijet, scaleRes;
  };
  struct Settings {
    std::string etaDir;
    std::vector<std::string> tiers, variables;
    // renormalize
    std::vector<std::string> renSets, renInputs;
    std::string renOutput;
    // common to the pre-combinations
    std::vector<double> ptBins;
    std::string fitFormula;
    double fitMin, fitMax;
    std::string fitOption;
    // precombinePF
    std::vector<std::string> pfSets;
    std::string pfInput;
    std::map<std::string, double> pfMinPt;
    std::string pfHistogram, pfFunction, pfOutput;
    // precombineHDM
    std::vector<std::string> hdmSets, hdmTiers, hdmInputs;
    std::string hdmMultijet, hdmCrecoil, hdmL2l3res, hdmHistogram, hdmFunction;
    std::vector<Variant> hdmVariants;

    static Settings parse(const nlohmann::json& json, const std::string& where);
  };

  explicit GlobalFitRenormPF(const Epoch& epoch);

  std::string name() const override { return "globalFitRenormPF"; }
  void run(const std::string& jecdataPath) override;

 private:
  void renormalize(const std::string& path) const;
  void precombinePf(const std::string& path) const;
  void precombineHdm(const std::string& path, const Variant& variant) const;

  Epoch epoch_;
  Settings settings_;
  TdrStyle style_;
};
