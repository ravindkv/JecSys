#pragma once
// chi2 scan of one parameter: it is fixed at each point of a grid and all other
// parameters are refitted. Writes scan.root (TGraph of delta chi2) and scan.json.
#include "FitMethod.h"

class MultiDimFit : public FitMethod {
 public:
  std::string name() const override { return "MultiDimFit"; }
  void run(FitContext& context) const override;
};
