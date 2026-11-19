#pragma once
// Impact of each nuisance on the result: the nuisance is fixed at its post-fit value
// +- its post-fit uncertainty, the fit is repeated, and the change of the JES at a few
// pT values and of every model parameter is recorded. Writes impacts.json.
#include "FitMethod.h"

class Impacts : public FitMethod {
 public:
  std::string name() const override { return "Impacts"; }
  void run(FitContext& context) const override;
};
