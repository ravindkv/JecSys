#pragma once
// Minimisation of a Chi2Function with TMinuit (through TFitter, command MINI), as in
// the old globalFitL3Res.C, so that the results are identical. Minuit2 can be added
// behind the same interface later (toDo.txt E1).
#include <string>
#include <vector>

#include "Chi2Function.h"
#include "TMatrixD.h"

struct ParameterSpec {
  std::string name;
  double start = 0;
  double step = 1;
  double lower = -100, upper = 100;
  bool fixed = false;
  bool isNuisance = false;
};

struct FitResult {
  std::vector<ParameterSpec> parameters;  // as given to the minimiser
  std::vector<double> values;
  std::vector<double> errors;             // 0 for a fixed parameter
  TMatrixD covariance;                    // nPar x nPar; rows/columns of fixed parameters are 0
  double chi2 = 0;
  int status = 0;                         // return code of MINI; 0 = converged
  int nFree = 0;

  int index(const std::string& name) const;  // -1 if unknown
};

class FitMinimizer {
 public:
  // printLevel: -1 quiet, 0 Minuit default output.
  static FitResult minimize(const Chi2Function& chi2, const std::vector<ParameterSpec>& parameters,
                            int printLevel = -1);

 private:
  // TFitter::SetFCN needs a plain C function, so the chi2 being minimised is reached
  // through this pointer. It is set on entry to minimize() and cleared on exit, and it
  // is the only static data in the package.
  static const Chi2Function* active_;
  static void fcn(int& nPar, double* gradient, double& value, double* par, int flag);
};

// Parameter list of a fit: the model parameters followed by one nuisance parameter per
// card nuisance (start 0, step 1, range +-100, as in the old code).
std::vector<ParameterSpec> makeParameterSpecs(const PhysicsModel& model, const Workspace& workspace);
