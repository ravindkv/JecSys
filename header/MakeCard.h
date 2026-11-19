#pragma once
// MakeCard: builds the datacard and the fit-ready shapes file of one epoch from the
// pre-fit file (was globalFitL3Res.C lines 354-1616: choose samples, load graphs,
// apply the FSR correction, build the nuisance sources). Everything it needs to know
// comes from config/MakeCard.json and config/Inputs.json.
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ChannelKind.h"
#include "ConfigFile.h"
#include "Datacard.h"
#include "Inputs.h"

struct SampleSettings {
  ChannelKind kind = ChannelKind::Response;
  int alpha = 30;                  // number in the graph name "<method>_<sample>_a<alpha>"
  bool hdmFsr = false;             // true: correct with fsr/hkfsr3_* (HDM); false: no FSR correction
  std::string crecoil;             // multijet only: graph of the recoil pT fraction
  std::optional<double> scale;     // flat scale nuisance (both methods)
  std::optional<double> hdmScale;  // second flat scale nuisance (both methods)
};

struct ExtraSource {
  std::string name;
  std::string object;                  // path below ratio/<etaBin>/
  bool fromRefIov = false;             // read from the reference IOV instead of this epoch
  std::vector<std::string> appliesTo;  // channel names
};

struct PfSample {
  std::string sample;
  int alpha = 30;
  double delta = 0;                   // dead zone, in %
  std::pair<double, double> ptRange;  // open interval
};

struct MakeCardConfig {
  std::pair<double, double> etaRange{0, 1.3};
  double ptMin = 15;
  std::vector<std::string> methods;
  std::vector<std::string> samples;
  std::map<std::string, SampleSettings> sampleSettings;
  std::vector<std::string> fsrEigenvectors;
  std::vector<ExtraSource> sources;
  std::vector<std::string> pfFractions;
  std::vector<PfSample> pfSamples;
  std::string jesRef;

  static MakeCardConfig fromJson(const nlohmann::json& settings, const std::string& where);
};

class MakeCard {
 public:
  MakeCard(MakeCardConfig config, const Inputs& inputs, std::string resolvedConfigText);

  // Writes input/forfit/card/<corLevel>/<epoch>/{datacard.txt, shapes.root}. Returns the card.
  // Throws JecSysError if an input is missing; existing outputs are then left untouched.
  Datacard run(const Epoch& epoch) const;

 private:
  const MakeCardConfig config_;
  const Inputs& inputs_;
  const std::string resolvedConfigText_;
};
