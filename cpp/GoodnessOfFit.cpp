#include "GoodnessOfFit.h"

#include <algorithm>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "JecSysError.h"
#include "TMath.h"
#include "TRandom3.h"
#include "TextTools.h"

void GoodnessOfFit::run(FitContext& context) const {
  Chi2Function& chi2 = context.chi2;
  const int nModel = chi2.nModelPar();

  const FitResult fit = FitMinimizer::minimize(chi2, context.parameters, context.options.verbose ? 0 : -1);
  const Chi2Breakdown breakdown = chi2.breakdown(fit.values.data());
  fitTools::printResult(context, fit, breakdown);
  const int nDof = fitTools::ndf(context, fit, breakdown);

  nlohmann::json result;
  result["epoch"] = context.workspace.card().epoch;
  result["chi2"] = breakdown.total;
  result["ndf"] = nDof;
  result["probability"] = TMath::Prob(breakdown.total, nDof);
  result["channels"] = nlohmann::json::array();
  for (std::size_t c = 0; c != chi2.channels().size(); ++c) {
    result["channels"].push_back({{"name", chi2.channels()[c]->name()},
                                  {"masked", chi2.isMasked(c)},
                                  {"chi2", breakdown.channelChi2[c]},
                                  {"points", breakdown.channelPoints[c]}});
  }

  // Toys: data thrown around the post-fit model. Each nuisance is drawn from its unit
  // Gaussian prior (frozen ones stay at their value); the model parameters are kept at
  // the best fit. Every toy is fitted like the data.
  const int nToys = context.options.nToys;
  if (nToys > 0) {
    const std::vector<std::vector<double>> observed = chi2.data();
    TRandom3 random(context.options.seed);
    std::vector<double> toyChi2;
    int nFailed = 0;
    for (int t = 0; t != nToys; ++t) {
      std::vector<double> truth = fit.values;
      for (std::size_t i = nModel; i != truth.size(); ++i) {
        if (!fit.parameters[i].fixed) truth[i] = random.Gaus(0, 1);
      }
      std::vector<std::vector<double>> toy;
      for (const auto& channel : chi2.channels()) {
        toy.push_back(channel->generate(truth.data(), truth.data() + nModel, random));
      }
      chi2.setData(toy);
      const FitResult toyFit = FitMinimizer::minimize(chi2, context.parameters, -1);
      if (toyFit.status != 0) ++nFailed;
      toyChi2.push_back(toyFit.chi2);
    }
    chi2.setData(observed);

    const auto nAbove = std::count_if(toyChi2.begin(), toyChi2.end(),
                                      [&](double value) { return value >= breakdown.total; });
    const double pValue = static_cast<double>(nAbove) / nToys;
    std::vector<double> sorted = toyChi2;
    std::sort(sorted.begin(), sorted.end());
    std::printf("Toys: %d thrown (seed %u), %d did not converge\n", nToys, context.options.seed, nFailed);
    std::printf("  median toy chi2 = %.2f, observed = %.2f, p-value = %.3f\n", sorted[sorted.size() / 2],
                breakdown.total, pValue);
    result["toys"] = {{"n", nToys},      {"seed", context.options.seed}, {"failed", nFailed},
                      {"pValue", pValue}, {"median", sorted[sorted.size() / 2]}, {"chi2", toyChi2}};
  }

  const std::string path = context.outputPath("gof", "json");
  textTools::makeDirs(textTools::dirName(path));
  std::ofstream out(path);
  if (!out) throw JecSysError("Cannot write " + path);
  out << result.dump(2) << "\n";
  std::cout << "Wrote " << path << "\n";
}
