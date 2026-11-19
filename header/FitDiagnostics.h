#pragma once
// Best fit, covariance, eigenvectors, chi2 per channel, post-fit graphs and nuisance
// pulls. Writes fit.root and result.json (was globalFitL3Res.C lines 1940-2800 without
// the plots). The objects under ratio/<etaBin>/ keep the names of the old
// jecdata<epoch>.root, so downstream tools and the parity test can read them.
#include "FitMethod.h"

class FitDiagnostics : public FitMethod {
 public:
  std::string name() const override { return "FitDiagnostics"; }
  void run(FitContext& context) const override;
};
