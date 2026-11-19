#pragma once
// Reprocess (was reprocess.C): merges the inputs of the analysis groups into the pre-fit
// file of an epoch (RECREATE):
//   1. <tier>/eta<..>/<type>_<sample>_a<alpha>: the graphs of every sample, read, cleaned
//      and corrected (uncleaned copies in orig/), and the event counts (GraphMaker);
//   2. <tier>/eta00-13/mass_<sample>_a<30|100>: the Z mass histograms;
//   3. ratio/eta<..>/herr*, hjes, hl1*, hsys*, hruncl: the JEC reference bands and the
//      L1 systematics (JecBands).
// Settings: input/config/Reprocess.json (ReprocessSettings). The order of the operations
// and the current ROOT directory follow the macro, because they decide where ROOT
// creates objects and which in-memory object a later Get() returns.
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "InputStep.h"
#include "ReprocessSettings.h"
#include "SampleTable.h"
#include "TdrStyle.h"

class TFile;
class TObject;

class Reprocess : public InputStep {
 public:
  // `jecInversion`: overrides jec.inversion of the settings if not empty.
  Reprocess(const Inputs& inputs, const Epoch& epoch, const std::string& jecInversion = "");
  ~Reprocess() override;

  std::string name() const override { return "reprocess"; }
  void run(const std::string& jecdataPath) override;

  const ReprocessSettings& settings() const { return settings_; }

 private:
  TFile& open(const std::string& path);
  void openInputs();
  void loadMassHistograms();
  void writeMassHistograms(TFile& out);

  Epoch epoch_;
  ReprocessSettings settings_;
  SampleTable samples_;
  TdrStyle style_;
  std::vector<std::unique_ptr<TFile>> files_;  // input files, open until the output is closed
  std::map<std::string, TDirectory*> inputs_;  // input key -> file or directory
  std::map<std::string, TObject*> mass_;       // e.g. "zee_ratio_30"
  std::vector<std::unique_ptr<TObject>> ownedMass_;
};
