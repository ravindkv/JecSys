// jecMakeInputs: the input creation (was mk_reprocess.C). Runs the steps of
// input/config/InputChain.json on the pre-fit file of each epoch. Run from the repository
// root, like runFit: all paths of the configuration are relative to it.
#include <filesystem>
#include <iostream>

#include "CommandLine.h"
#include "InputChain.h"
#include "Inputs.h"
#include "JecSysError.h"
#include "TROOT.h"
#include "TextTools.h"

int main(int argc, char** argv) {
  return runMain("jecMakeInputs", [&]() {
    CommandLine cli("jecMakeInputs", "input/jecMakeInputs -e <epoch>|all [options]",
                    "the input creation: runs the steps of input/config/InputChain.json on the pre-fit\n"
                    "file of each epoch (reprocess -> softRad x2 -> globalFitSyst -> globalFitRenormPF)");
    cli.addOption("epoch", 'e', "name", "epoch of config/Inputs.json, a comma-separated list, or 'all'");
    cli.addOption("steps", 0, "a,b", "run only these steps (reprocess, softRad, globalFitSyst, globalFitRenormPF)");
    cli.addOption("file", 0, "path", "pre-fit file (one epoch only; default: 'forfit' of config/Inputs.json)");
    cli.addOption("outdir", 0, "dir", "write the pre-fit files to <dir>/ (same file names) instead");
    cli.addOption("snapshot", 0, "dir", "copy the file after every step to <dir>/<epoch>/<tag>.root");
    cli.addOption("inputs", 0, "path", "epoch registry (default: config/Inputs.json)");
    cli.addOption("jec-inversion", 0, "mode", "getx, memo or secant: overrides jec.inversion of Reprocess.json");
    cli.addOption("verbose", 'v', "", "print the per-bin diagnostics of softRad");
    cli.setFooter(
        "reprocess creates the file (RECREATE); the later steps update it in place (UPDATE, as the\n"
        "macros), so a subset of them (--steps) must run on a file that has not seen them yet.");
    cli.parse(argc, argv);
    if (cli.helpRequested() || !cli.has("epoch") || !cli.positional().empty()) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    const std::string inputsPath = cli.value("inputs", "config/Inputs.json");
    if (!textTools::fileExists(inputsPath)) {
      throw JecSysError("Cannot find " + inputsPath + "; run jecMakeInputs from the repository root");
    }
    const Inputs inputs(inputsPath);
    const std::string epochName = cli.value("epoch");
    std::vector<std::string> epochs;  // 'all' or a comma-separated list
    if (epochName == "all") {
      epochs = inputs.epochNames();
    } else {
      for (const std::string& name : textTools::split(epochName, ',')) epochs.push_back(inputs.epoch(name).name);
    }
    if (cli.has("file") && epochs.size() != 1) throw JecSysError("--file needs exactly one epoch");
    if (cli.has("file") && cli.has("outdir")) throw JecSysError("--file and --outdir exclude each other");
    const auto output = [&](const std::string& epoch) {
      if (cli.has("file")) return cli.value("file");
      const std::string& forfit = inputs.epoch(epoch).forfit;
      if (!cli.has("outdir")) return forfit;
      return cli.value("outdir") + "/" + std::filesystem::path(forfit).filename().string();
    };

    InputChain::Options options;
    if (cli.has("steps")) {
      for (const std::string& step : textTools::split(cli.value("steps"), ',')) options.steps.insert(step);
    }
    options.snapshotDir = cli.value("snapshot");
    options.verbose = cli.has("verbose");
    options.jecInversion = cli.value("jec-inversion");

    gROOT->SetBatch(true);
    // Build every chain first: all settings are checked before any file is touched.
    std::vector<InputChain> chains;
    for (const std::string& name : epochs) chains.emplace_back(inputs, inputs.epoch(name), options);
    for (std::size_t i = 0; i != epochs.size(); ++i) {
      chains[i].run(output(epochs[i]));
    }
    return 0;
  });
}
