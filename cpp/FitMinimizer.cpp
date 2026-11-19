#include "FitMinimizer.h"

#include "JecSysError.h"
#include "TFitter.h"
#include "TMinuit.h"

const Chi2Function* FitMinimizer::active_ = nullptr;

void FitMinimizer::fcn(int&, double*, double& value, double* par, int) { value = (*active_)(par); }

int FitResult::index(const std::string& name) const {
  for (std::size_t i = 0; i != parameters.size(); ++i) {
    if (parameters[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

FitResult FitMinimizer::minimize(const Chi2Function& chi2, const std::vector<ParameterSpec>& parameters,
                                 int printLevel) {
  const int nPar = static_cast<int>(parameters.size());
  if (nPar != chi2.nPar()) throw JecSysError("FitMinimizer: wrong number of parameters");

  struct ActiveGuard {
    explicit ActiveGuard(const Chi2Function* f) { active_ = f; }
    ~ActiveGuard() { active_ = nullptr; }
  } guard(&chi2);

  TFitter fitter(nPar);
  double level = printLevel;
  fitter.ExecuteCommand("SET PRINT", &level, 1);
  if (printLevel < 0) fitter.ExecuteCommand("SET NOWARNINGS", nullptr, 0);
  fitter.SetFCN(&FitMinimizer::fcn);
  for (int i = 0; i != nPar; ++i) {
    const ParameterSpec& p = parameters[i];
    fitter.SetParameter(i, p.name.c_str(), p.start, p.step, p.lower, p.upper);
  }
  FitResult result;
  result.parameters = parameters;
  for (int i = 0; i != nPar; ++i) {
    if (parameters[i].fixed) {
      fitter.FixParameter(i);
    } else {
      ++result.nFree;
    }
  }
  if (result.nFree == 0) throw JecSysError("FitMinimizer: every parameter is frozen");

  result.status = fitter.ExecuteCommand("MINI", nullptr, 0);

  // mnemat returns the matrix of the free parameters only.
  TMatrixD freeMatrix(result.nFree, result.nFree);
  fitter.GetMinuit()->mnemat(freeMatrix.GetMatrixArray(), result.nFree);
  result.covariance.ResizeTo(nPar, nPar);
  result.covariance.Zero();
  std::vector<int> freeIndex;
  for (int i = 0; i != nPar; ++i) {
    if (!parameters[i].fixed) freeIndex.push_back(i);
  }
  for (int i = 0; i != result.nFree; ++i) {
    for (int j = 0; j != result.nFree; ++j) {
      result.covariance[freeIndex[i]][freeIndex[j]] = freeMatrix[i][j];
    }
  }

  result.values.resize(nPar);
  result.errors.resize(nPar);
  for (int i = 0; i != nPar; ++i) {
    result.values[i] = fitter.GetParameter(i);
    result.errors[i] = parameters[i].fixed ? 0. : fitter.GetParError(i);
  }
  result.chi2 = chi2(result.values.data());
  return result;
}

std::vector<ParameterSpec> makeParameterSpecs(const PhysicsModel& model, const Workspace& workspace) {
  std::vector<ParameterSpec> specs;
  for (const FitParameter& p : model.parameters()) {
    ParameterSpec spec;
    spec.name = p.name;
    spec.start = p.start;
    spec.step = p.step;
    spec.lower = p.lower;
    spec.upper = p.upper;
    specs.push_back(spec);
  }
  for (const WorkspaceNuisance& n : workspace.nuisances()) {
    ParameterSpec spec;
    spec.name = n.name;
    spec.isNuisance = true;
    specs.push_back(spec);
  }
  return specs;
}
