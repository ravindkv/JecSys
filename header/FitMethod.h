#pragma once
// A fit method is what "jecFit -M <name>" runs on a workspace. Every method gets the same FitContext, so all of them work on exactly
// the same fit problem.
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Chi2Function.h"
#include "FitMinimizer.h"
#include "PhysicsModel.h"
#include "Workspace.h"

struct FitOptions {
  std::string method = "FitDiagnostics";
  std::string name;                           // -n: suffix of the output files
  std::string outDir;                         // default: output/postfit/<corLevel>/<epoch>
  std::map<std::string, double> setParameters;   // start (or frozen) values
  std::vector<std::string> freezePatterns;       // regular expressions on parameter names
  std::vector<std::string> maskPatterns;         // regular expressions on channel names
  bool verbose = false;
  // toys (GoodnessOfFit)
  int nToys = 0;
  unsigned seed = 12345;
  // MultiDimFit
  std::string scanParameter;
  double scanLow = 0, scanHigh = 0;
  int scanPoints = 20;
  // Impacts
  std::vector<double> ptPoints{30, 100, 300, 1000};
};

struct FitContext {
  const Workspace& workspace;
  const PhysicsModel& model;
  Chi2Function& chi2;                     // non-const only so that toys can replace the data
  std::vector<ParameterSpec> parameters;  // with --setParameters and --freezeParameters applied
  const FitOptions& options;

  // Output path "<outDir>/<stem><name>.<extension>".
  std::string outputPath(const std::string& stem, const std::string& extension) const;
  // Closure reference of the first response channel at `pt` (1 if it has none), and
  // the JES that channel sees: model / reference.
  double jesRef(double pt) const;
  double jes(double pt, const double* par) const;
};

class FitMethod {
 public:
  virtual ~FitMethod() = default;
  virtual std::string name() const = 0;
  virtual void run(FitContext& context) const = 0;
};

std::unique_ptr<FitMethod> makeFitMethod(const std::string& name);  // throws if unknown
std::vector<std::string> fitMethodNames();

// Set up the context from the options (masks, start values, frozen parameters) and run
// the chosen method. Used by jecFit and runFit.
void runFitMethod(const Workspace& workspace, const FitOptions& options);

// Helpers shared by the methods.
namespace fitTools {
// Print parameters and chi2 of a fit.
void printResult(const FitContext& context, const FitResult& result, const Chi2Breakdown& breakdown);
// Number of degrees of freedom as the old code counts it: points - free model parameters.
int ndf(const FitContext& context, const FitResult& result, const Chi2Breakdown& breakdown);
std::string gitHash();
}  // namespace fitTools
