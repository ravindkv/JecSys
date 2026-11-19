#pragma once
// The chi2 of the global fit (was jesFitter in globalFitL3Res.C):
//   chi2 = sum over channels and points of [(data + shifts - model) / sigma]^2
//        + sum over nuisances of theta^2        (unit Gaussian constraints)
//        + sum over penalized model parameters of p^2
// Evaluating it changes nothing: no globals, no output graphs filled on the side.
// Parameter vector: [model parameters..., nuisance parameters...] in card order.
#include <memory>
#include <string>
#include <vector>

#include "Channel.h"
#include "PhysicsModel.h"
#include "Workspace.h"

struct Chi2Breakdown {
  double total = 0;
  int nPoints = 0;  // points of unmasked channels + penalized parameters (was Nk)
  std::vector<double> channelChi2;
  std::vector<int> channelPoints;
  double nuisanceChi2 = 0;
  double penaltyChi2 = 0;
};

class Chi2Function {
 public:
  // `maskedChannels`: channels left out of the chi2 (jecFit --mask).
  Chi2Function(const Workspace& workspace, const PhysicsModel& model,
               const std::vector<bool>& maskedChannels = {});

  int nModelPar() const { return nModelPar_; }
  int nNuisances() const { return nNuisances_; }
  int nPar() const { return nModelPar_ + nNuisances_; }

  double operator()(const double* par) const;
  Chi2Breakdown breakdown(const double* par) const;

  const std::vector<std::unique_ptr<Channel>>& channels() const { return channels_; }
  bool isMasked(std::size_t channel) const { return masked_[channel]; }
  const PhysicsModel& model() const { return model_; }

  // Replace the data of every channel by pseudo-data (toys).
  void setData(const std::vector<std::vector<double>>& data);
  std::vector<std::vector<double>> data() const;

 private:
  const PhysicsModel& model_;
  std::vector<std::unique_ptr<Channel>> channels_;
  std::vector<bool> masked_;
  std::vector<bool> penalized_;
  int nModelPar_ = 0;
  int nNuisances_ = 0;
};
