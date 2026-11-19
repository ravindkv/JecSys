#include "FitMethod.h"

#include <cmath>
#include <cstdio>
#include <iostream>

#include "FitDiagnostics.h"
#include "GoodnessOfFit.h"
#include "Impacts.h"
#include "JecSysError.h"
#include "MultiDimFit.h"
#include "TMath.h"
#include "TextTools.h"

std::string FitContext::outputPath(const std::string& stem, const std::string& extension) const {
  std::string dir = options.outDir;
  if (dir.empty()) dir = "output/postfit/" + workspace.card().corLevel + "/" + workspace.card().epoch;
  return dir + "/" + stem + (options.name.empty() ? "" : "_" + options.name) + "." + extension;
}

double FitContext::jesRef(double pt) const {
  for (const WorkspaceChannel& channel : workspace.channels()) {
    if (channel.kind == ChannelKind::PfFraction) continue;
    return channel.jesRef ? channel.jesRef->Interpolate(pt) : 1.;
  }
  return 1.;
}

double FitContext::jes(double pt, const double* par) const {
  return model.response(pt, par) / jesRef(pt);
}

std::vector<std::string> fitMethodNames() {
  return {"FitDiagnostics", "GoodnessOfFit", "Impacts", "MultiDimFit"};
}

std::unique_ptr<FitMethod> makeFitMethod(const std::string& name) {
  if (name == "FitDiagnostics") return std::make_unique<FitDiagnostics>();
  if (name == "GoodnessOfFit") return std::make_unique<GoodnessOfFit>();
  if (name == "Impacts") return std::make_unique<Impacts>();
  if (name == "MultiDimFit") return std::make_unique<MultiDimFit>();
  throw JecSysError("Unknown method '" + name + "' (known: " +
                    textTools::join(fitMethodNames(), ", ") + ")");
}

void runFitMethod(const Workspace& workspace, const FitOptions& options) {
  const std::unique_ptr<FitMethod> method = makeFitMethod(options.method);
  const std::unique_ptr<PhysicsModel> model =
      makePhysicsModel(workspace.modelName(), workspace.modelConfig());

  // --mask: every pattern must match at least one channel, so a typo is an error.
  std::vector<bool> masked(workspace.channels().size(), false);
  for (const std::string& pattern : options.maskPatterns) {
    bool any = false;
    for (std::size_t i = 0; i != masked.size(); ++i) {
      if (textTools::regexMatch(pattern, workspace.channels()[i].name)) masked[i] = any = true;
    }
    if (!any) throw JecSysError("--mask '" + pattern + "' matches no channel");
  }
  Chi2Function chi2(workspace, *model, masked);

  std::vector<ParameterSpec> parameters = makeParameterSpecs(*model, workspace);
  const auto find = [&parameters](const std::string& name) -> ParameterSpec* {
    for (ParameterSpec& p : parameters) {
      if (p.name == name) return &p;
    }
    return nullptr;
  };
  for (const auto& [name, value] : options.setParameters) {
    ParameterSpec* p = find(name);
    if (!p) throw JecSysError("--setParameters: unknown parameter '" + name + "'");
    p->start = value;
  }
  for (const std::string& pattern : options.freezePatterns) {
    bool any = false;
    for (ParameterSpec& p : parameters) {
      if (textTools::regexMatch(pattern, p.name)) p.fixed = any = true;
    }
    if (!any) throw JecSysError("--freezeParameters '" + pattern + "' matches no parameter");
  }

  FitContext context{workspace, *model, chi2, parameters, options};
  method->run(context);
}

namespace fitTools {

int ndf(const FitContext& context, const FitResult& result, const Chi2Breakdown& breakdown) {
  int nFreeModel = 0;
  for (int i = 0; i != context.chi2.nModelPar(); ++i) {
    if (!result.parameters[i].fixed) ++nFreeModel;
  }
  return breakdown.nPoints - nFreeModel;
}

void printResult(const FitContext& context, const FitResult& result, const Chi2Breakdown& breakdown) {
  const int nModel = context.chi2.nModelPar();
  const int nDof = ndf(context, result, breakdown);
  std::printf("\nFit of %s (%s, %s) with model %s: Minuit status %d\n",
              context.workspace.card().epoch.c_str(), context.workspace.card().corLevel.c_str(),
              context.workspace.card().etaBin.c_str(), context.model.name().c_str(), result.status);
  std::printf("chi2 / ndf = %.3f / %d   (%d points, %d model parameters, %d nuisances; prob %.3g)\n",
              breakdown.total, nDof, breakdown.nPoints, nModel, context.chi2.nNuisances(),
              TMath::Prob(breakdown.total, nDof));
  std::printf("  data %.3f, nuisances %.3f, parameter penalty %.3f\n",
              breakdown.total - breakdown.nuisanceChi2 - breakdown.penaltyChi2, breakdown.nuisanceChi2,
              breakdown.penaltyChi2);
  std::printf("\n%-22s %10s %8s\n", "channel", "chi2", "points");
  for (std::size_t i = 0; i != context.chi2.channels().size(); ++i) {
    if (context.chi2.isMasked(i)) {
      std::printf("%-22s %10s %8s\n", context.chi2.channels()[i]->name().c_str(), "masked", "-");
    } else {
      std::printf("%-22s %10.2f %8d\n", context.chi2.channels()[i]->name().c_str(),
                  breakdown.channelChi2[i], breakdown.channelPoints[i]);
    }
  }
  std::printf("\n%-32s %10s %8s\n", "parameter", "value", "error");
  for (std::size_t i = 0; i != result.values.size(); ++i) {
    if (static_cast<int>(i) == nModel) std::printf("%-32s\n", "--- nuisances ---");
    std::printf("%-32s %10.4f %8.4f%s\n", result.parameters[i].name.c_str(), result.values[i],
                result.errors[i], result.parameters[i].fixed ? "  (frozen)" : "");
  }
  std::printf("\n");
  if (result.status != 0) {
    std::cerr << "WARNING: Minuit did not converge (status " << result.status << ")\n";
  }
}

std::string gitHash() {
  std::string hash;
  if (FILE* pipe = popen("git rev-parse --short HEAD 2>/dev/null", "r")) {
    char buffer[64];
    if (fgets(buffer, sizeof(buffer), pipe)) hash = buffer;
    pclose(pipe);
  }
  while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r')) hash.pop_back();
  return hash.empty() ? "unknown" : hash;
}

}  // namespace fitTools
