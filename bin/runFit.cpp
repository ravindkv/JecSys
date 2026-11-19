// runFit: driver for the fit half of the chain. For each epoch it runs
//   card (jecMakeCard) -> workspace (jecWorkspace) -> fit (jecFit) -> plot (jecPlotFit)
// by calling the same code as the three executables.
#include <algorithm>
#include <iostream>
#include <sstream>

#include "CommandLine.h"
#include "FitChain.h"
#include "FitCommandLine.h"
#include "FitPlotter.h"
#include "JecSysError.h"
#include "TextTools.h"

namespace {

std::string epochTable(const Inputs& inputs) {
  std::ostringstream out;
  out << "Epochs in config/Inputs.json:\n";
  for (const std::string& name : inputs.epochNames()) {
    const Epoch& epoch = inputs.epoch(name);
    char line[256];
    std::snprintf(line, sizeof(line), "  %-10s %-8s %-10s refIov %-10s ./runFit -e %s\n", name.c_str(),
                  epoch.era.c_str(), epoch.corLevel.c_str(),
                  epoch.isRefIov() ? "self" : epoch.refIov.c_str(), name.c_str());
    out << line;
  }
  out << "Run everything: ./runFit -e all\n"
      << "The pre-fit files input/forfit/reprocess/jecdata<epoch>.root are made by: make inputs   (or: make -C input <epoch>)";
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  return runMain("runFit", [&]() {
    CommandLine cli("runFit", "runFit -e <epoch|all> [-s <step>[,<step>...]] [fit options]",
                    "JEC L3Res global fit: datacard -> workspace -> fit -> plots");
    cli.addOption("epoch", 'e', "name", "epoch from config/Inputs.json, or 'all' (reference IOVs first)");
    cli.addOption("step", 's', "list", "steps to run: card, workspace, fit, plot (default: all four)");
    cli.addOption("physicsModel", 0, "model", "physics model (default: JesModel9)");
    cli.addOption("config", 0, "dir", "configuration directory (default: config)");
    cli.addOption("inputs", 0, "file", "epoch registry (default: <config>/Inputs.json)");
    fitCommandLine::addOptions(cli);
    cli.parse(argc, argv);

    const std::string configDir = cli.value("config", "config");
    const Inputs inputs(cli.value("inputs", configDir + "/Inputs.json"));
    cli.setFooter(epochTable(inputs));
    if (cli.helpRequested() || !cli.has("epoch")) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }

    const std::vector<std::string> steps = textTools::split(cli.value("step", "card,workspace,fit,plot"), ',');
    for (const std::string& step : steps) {
      if (step != "card" && step != "workspace" && step != "fit" && step != "plot") {
        throw JecSysError("unknown step '" + step + "' (known: card, workspace, fit, plot)");
      }
    }
    const auto runs = [&steps](const char* step) {
      return std::find(steps.begin(), steps.end(), step) != steps.end();
    };
    const FitOptions options = fitCommandLine::parse(cli);
    const std::string model = cli.value("physicsModel", "JesModel9");

    const std::string requested = cli.value("epoch");
    const std::vector<std::string> epochs =
        requested == "all" ? inputs.epochsInDependencyOrder() : std::vector<std::string>{requested};
    for (const std::string& name : epochs) {
      const Epoch& epoch = inputs.epoch(name);
      std::cout << "\n===== " << name << " (" << epoch.corLevel << ") =====\n";
      if (runs("card")) fitChain::makeCard(inputs, name, configDir);
      if (runs("workspace")) {
        fitChain::makeWorkspace(inputs, inputs.cardPath(epoch), inputs.workspacePath(epoch), model, configDir);
      }
      if (runs("fit")) {
        FitOptions epochOptions = options;
        if (epochOptions.outDir.empty()) epochOptions.outDir = inputs.postfitDir(epoch);
        fitChain::fit(inputs.workspacePath(epoch), epochOptions);
      }
      if (runs("plot")) {
        if (options.method != "FitDiagnostics") {
          std::cout << "  note: the plot step needs -M FitDiagnostics; skipped\n";
        } else {
          const std::string dir = options.outDir.empty() ? inputs.postfitDir(epoch) : options.outDir;
          FitPlotter::run(dir + "/fit" + (options.name.empty() ? "" : "_" + options.name) + ".root",
                          dir + "/plots");
        }
      }
    }
    return 0;
  });
}
