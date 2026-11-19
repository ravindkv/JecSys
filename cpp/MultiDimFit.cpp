#include "MultiDimFit.h"

#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "JecSysError.h"
#include "OutputFile.h"
#include "TGraph.h"

void MultiDimFit::run(FitContext& context) const {
  const Chi2Function& chi2 = context.chi2;
  const FitOptions& options = context.options;
  if (options.scanParameter.empty()) throw JecSysError("MultiDimFit needs -P <parameter>");
  if (options.scanPoints < 2) throw JecSysError("MultiDimFit needs --points >= 2");

  const FitResult nominal = FitMinimizer::minimize(chi2, context.parameters, -1);
  fitTools::printResult(context, nominal, chi2.breakdown(nominal.values.data()));
  const int index = nominal.index(options.scanParameter);
  if (index < 0) throw JecSysError("MultiDimFit: unknown parameter '" + options.scanParameter + "'");
  if (nominal.parameters[index].fixed) {
    throw JecSysError("MultiDimFit: parameter '" + options.scanParameter + "' is frozen");
  }

  // Default range: best fit +- 3 sigma.
  double low = options.scanLow, high = options.scanHigh;
  if (low == high) {
    low = nominal.values[index] - 3 * nominal.errors[index];
    high = nominal.values[index] + 3 * nominal.errors[index];
  }

  TGraph scan(options.scanPoints);
  scan.SetName(("scan_" + options.scanParameter).c_str());
  scan.SetTitle((";" + options.scanParameter + ";#Delta#chi^{2}").c_str());
  nlohmann::json points = nlohmann::json::array();
  std::printf("Scan of %s (best fit %.4f +- %.4f, chi2 %.3f)\n%12s %12s\n", options.scanParameter.c_str(),
              nominal.values[index], nominal.errors[index], nominal.chi2, "value", "delta chi2");
  for (int i = 0; i != options.scanPoints; ++i) {
    const double value = low + (high - low) * i / (options.scanPoints - 1);
    std::vector<ParameterSpec> specs = context.parameters;
    for (std::size_t k = 0; k != specs.size(); ++k) specs[k].start = nominal.values[k];
    specs[index].start = value;
    specs[index].fixed = true;
    const FitResult fit = FitMinimizer::minimize(chi2, specs, -1);
    const double delta = fit.chi2 - nominal.chi2;
    scan.SetPoint(i, value, delta);
    points.push_back({{"value", value}, {"deltaChi2", delta}, {"minuitStatus", fit.status}});
    std::printf("%12.4f %12.4f\n", value, delta);
  }

  OutputFile out(context.outputPath("scan_" + options.scanParameter, "root"));
  out.write(scan.GetName(), scan);
  out.commit();

  nlohmann::json result;
  result["epoch"] = context.workspace.card().epoch;
  result["parameter"] = options.scanParameter;
  result["bestFit"] = nominal.values[index];
  result["error"] = nominal.errors[index];
  result["chi2"] = nominal.chi2;
  result["points"] = points;
  const std::string path = context.outputPath("scan_" + options.scanParameter, "json");
  std::ofstream json(path);
  if (!json) throw JecSysError("Cannot write " + path);
  json << result.dump(2) << "\n";
  std::cout << "Wrote " << out.path() << " and " << path << "\n";
}
