#pragma once
// chi2 / ndf of the fit, in total and per channel. With -t N it also throws N toy data
// sets from the post-fit model and gives the fraction with a larger chi2. Writes gof.json.
#include "FitMethod.h"

class GoodnessOfFit : public FitMethod {
 public:
  std::string name() const override { return "GoodnessOfFit"; }
  void run(FitContext& context) const override;
};
