#include "FitCommandLine.h"

#include "JecSysError.h"
#include "TextTools.h"

namespace fitCommandLine {

void addOptions(CommandLine& cli) {
  cli.addOption("method", 'M', "name",
                "method (default: FitDiagnostics; known: " + textTools::join(fitMethodNames(), ", ") + ")");
  cli.addOption("name", 'n', "label", "label added to the output file names");
  cli.addOption("outDir", 0, "dir", "output directory (default: output/postfit/<corLevel>/<epoch>)");
  cli.addOption("setParameters", 0, "a=1,b=0", "start values (the value used if the parameter is frozen)");
  cli.addOption("freezeParameters", 0, "re,re", "fix the parameters whose names match (regular expressions)");
  cli.addOption("mask", 0, "re,re", "leave the channels whose names match out of the chi2");
  cli.addOption("toys", 't', "N", "GoodnessOfFit: number of toy data sets");
  cli.addOption("seed", 0, "s", "GoodnessOfFit: random seed (default: 12345)");
  cli.addOption("parameter", 'P', "name", "MultiDimFit: parameter to scan");
  cli.addOption("range", 0, "lo:hi", "MultiDimFit: scan range (default: best fit +- 3 sigma)");
  cli.addOption("points", 0, "N", "MultiDimFit: number of scan points (default: 20)");
  cli.addOption("ptPoints", 0, "a,b,c", "Impacts: pT values for the JES impact (default: 30,100,300,1000)");
  cli.addOption("verbose", 'v', "", "print the Minuit output");
}

FitOptions parse(const CommandLine& cli) {
  FitOptions options;
  options.method = cli.value("method", options.method);
  options.name = cli.value("name");
  options.outDir = cli.value("outDir");
  options.verbose = cli.has("verbose");
  if (cli.has("setParameters")) {
    for (const std::string& item : textTools::split(cli.value("setParameters"), ',')) {
      const auto pair = textTools::split(item, '=');
      if (pair.size() != 2) throw JecSysError("--setParameters expects name=value[,name=value...]");
      options.setParameters[pair[0]] = textTools::toDouble(pair[1], "--setParameters " + pair[0]);
    }
  }
  if (cli.has("freezeParameters")) options.freezePatterns = textTools::split(cli.value("freezeParameters"), ',');
  if (cli.has("mask")) options.maskPatterns = textTools::split(cli.value("mask"), ',');
  if (cli.has("toys")) options.nToys = textTools::toInt(cli.value("toys"), "--toys");
  if (cli.has("seed")) options.seed = static_cast<unsigned>(textTools::toInt(cli.value("seed"), "--seed"));
  options.scanParameter = cli.value("parameter");
  if (cli.has("range")) {
    const auto limits = textTools::split(cli.value("range"), ':');
    if (limits.size() != 2) throw JecSysError("--range expects lo:hi");
    options.scanLow = textTools::toDouble(limits[0], "--range");
    options.scanHigh = textTools::toDouble(limits[1], "--range");
  }
  if (cli.has("points")) options.scanPoints = textTools::toInt(cli.value("points"), "--points");
  if (cli.has("ptPoints")) {
    options.ptPoints.clear();
    for (const std::string& item : textTools::split(cli.value("ptPoints"), ',')) {
      options.ptPoints.push_back(textTools::toDouble(item, "--ptPoints"));
    }
  }
  return options;
}

}  // namespace fitCommandLine
