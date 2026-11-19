#pragma once
// The workspace: one self-contained ROOT file that holds a complete fit problem
//. It stores the datacard text, every object the card
// refers to, and the physics model with its resolved configuration. All jecFit
// methods read this file and nothing else.
//
//   card/text                  the datacard (defines channels, nuisances, groups, order)
//   channels/<channel>/data    TGraphErrors
//   channels/<channel>/<aux>   crecoil (TGraphErrors), jesRef, fsr (TH1D)
//   nuisances/<name>/<channel> TH1D of a shape nuisance in one channel
//   aux/ptBinning              TH1D whose binning is used for the output histograms
//   model/name, model/config   physics model and its resolved JSON configuration
//   _meta/...                  provenance
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Datacard.h"
#include "TGraphErrors.h"
#include "TH1D.h"

struct WorkspaceChannel {
  std::string name;
  ChannelKind kind = ChannelKind::Response;
  std::string fraction;           // PfFraction: which PF fraction ("chf", "nhf", "nef")
  double delta = 0;               // PfFraction: dead zone in %
  double ptLow = 0, ptHigh = 0;   // PfFraction: open pT interval used in the chi2
  std::unique_ptr<TGraphErrors> data;
  std::unique_ptr<TGraphErrors> crecoil;  // Multijet: recoil pT fraction, same point order as data
  std::unique_ptr<TH1D> jesRef;           // optional closure reference the model is divided by
  std::unique_ptr<TH1D> fsr;              // optional FSR correction that was applied (for output)
};

struct WorkspaceNuisance {
  std::string name;
  CardNuisance::Type type = CardNuisance::Type::Shape;
  std::vector<std::optional<double>> values;  // per channel, as in the card
  std::vector<std::unique_ptr<TH1D>> shapes;  // per channel; null if flat or not acting
};

class Workspace {
 public:
  // Resolve every object the card refers to. Throws JecSysError if one is missing.
  static Workspace fromCard(const Datacard& card, const std::string& modelName,
                            const std::string& modelConfig);
  static Workspace read(const std::string& path);
  void write(const std::string& path, const std::string& cardPath) const;

  const Datacard& card() const { return card_; }
  const std::vector<WorkspaceChannel>& channels() const { return channels_; }
  const std::vector<WorkspaceNuisance>& nuisances() const { return nuisances_; }
  const std::string& modelName() const { return modelName_; }
  const std::string& modelConfig() const { return modelConfig_; }
  const TH1D* ptBinning() const { return ptBinning_.get(); }  // may be null
  const std::string& path() const { return path_; }

  int channelIndex(const std::string& name) const { return card_.channelIndex(name); }
  int nuisanceIndex(const std::string& name) const { return card_.nuisanceIndex(name); }

 private:
  Datacard card_;
  std::vector<WorkspaceChannel> channels_;
  std::vector<WorkspaceNuisance> nuisances_;
  std::string modelName_;
  std::string modelConfig_;
  std::unique_ptr<TH1D> ptBinning_;
  std::string path_;
};
