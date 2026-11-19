#pragma once
// Plots of a fit result. Reads only fit.root (written by jecFit -M FitDiagnostics), so
// plots can be remade without refitting:
//   <stem>_prefit.pdf    data as they enter the fit, with the fitted JES and its uncertainty
//   <stem>_postfit.pdf   data with the fitted nuisance shifts applied
//   <stem>_pf.pdf        PF energy fractions (data - MC) with the model
//   <stem>_pulls.pdf     post-fit values and uncertainties of the nuisances
// These are new, simpler plots; the eight plots of globalFitL3Res.C are not reproduced.
#include <string>

class FitPlotter {
 public:
  // Writes the plots next to `fitFile`, or into `outDir` if it is not empty.
  static void run(const std::string& fitFile, const std::string& outDir);
};
