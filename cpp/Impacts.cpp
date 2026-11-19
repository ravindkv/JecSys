#include "Impacts.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "JecSysError.h"
#include "TextTools.h"

void Impacts::run(FitContext& context) const {
  const Chi2Function& chi2 = context.chi2;
  const int nModel = chi2.nModelPar();
  const std::vector<double>& ptPoints = context.options.ptPoints;
  if (ptPoints.empty()) throw JecSysError("Impacts: --ptPoints is empty");

  const FitResult nominal = FitMinimizer::minimize(chi2, context.parameters, -1);
  fitTools::printResult(context, nominal, chi2.breakdown(nominal.values.data()));

  // Refit with one nuisance fixed, starting from the nominal best fit.
  const auto refit = [&](int index, double value) {
    std::vector<ParameterSpec> specs = context.parameters;
    for (std::size_t i = 0; i != specs.size(); ++i) specs[i].start = nominal.values[i];
    specs[index].start = value;
    specs[index].fixed = true;
    return FitMinimizer::minimize(chi2, specs, -1);
  };

  struct Row {
    std::string name;
    double value, error;
    std::vector<double> jesUp, jesDown;  // change of the JES at each pT, in %
    nlohmann::json parameters;
  };
  std::vector<Row> rows;
  for (std::size_t i = nModel; i != nominal.values.size(); ++i) {
    if (nominal.parameters[i].fixed) continue;
    const int cardIndex = static_cast<int>(i) - nModel;
    if (context.workspace.card().nuisances[cardIndex].isInactive()) continue;
    Row row{nominal.parameters[i].name, nominal.values[i], nominal.errors[i], {}, {}, nlohmann::json::array()};
    const FitResult up = refit(static_cast<int>(i), row.value + row.error);
    const FitResult down = refit(static_cast<int>(i), row.value - row.error);
    for (const double pt : ptPoints) {
      const double jes = context.jes(pt, nominal.values.data());
      row.jesUp.push_back(100 * (context.jes(pt, up.values.data()) - jes));
      row.jesDown.push_back(100 * (context.jes(pt, down.values.data()) - jes));
    }
    for (int k = 0; k != nModel; ++k) {
      row.parameters.push_back({{"name", nominal.parameters[k].name},
                                {"up", up.values[k] - nominal.values[k]},
                                {"down", down.values[k] - nominal.values[k]}});
    }
    rows.push_back(row);
  }

  // Largest impact on the JES (at any of the pT points) first.
  const auto size = [](const Row& row) {
    double largest = 0;
    for (std::size_t k = 0; k != row.jesUp.size(); ++k) {
      largest = std::max({largest, std::fabs(row.jesUp[k]), std::fabs(row.jesDown[k])});
    }
    return largest;
  };
  std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) { return size(a) > size(b); });

  std::printf("Impact on the JES in %% when a nuisance is fixed at +1 / -1 post-fit sigma\n");
  std::printf("%-32s %14s", "nuisance", "pull");
  for (const double pt : ptPoints) std::printf("  %7.0f GeV    ", pt);
  std::printf("\n");
  nlohmann::json result;
  result["epoch"] = context.workspace.card().epoch;
  result["ptPoints"] = ptPoints;
  result["impacts"] = nlohmann::json::array();
  for (const Row& row : rows) {
    std::printf("%-32s %+6.2f +- %4.2f", row.name.c_str(), row.value, row.error);
    for (std::size_t k = 0; k != ptPoints.size(); ++k) {
      std::printf("  %+6.3f/%+6.3f", row.jesUp[k], row.jesDown[k]);
    }
    std::printf("\n");
    result["impacts"].push_back({{"name", row.name},      {"value", row.value},
                                 {"error", row.error},    {"jesUpPercent", row.jesUp},
                                 {"jesDownPercent", row.jesDown}, {"parameters", row.parameters}});
  }

  const std::string path = context.outputPath("impacts", "json");
  textTools::makeDirs(textTools::dirName(path));
  std::ofstream out(path);
  if (!out) throw JecSysError("Cannot write " + path);
  out << result.dump(2) << "\n";
  std::cout << "Wrote " << path << "\n";
}
