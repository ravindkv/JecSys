// jecMakeCard: pre-fit file -> datacard.txt + shapes.root.
#include <iostream>

#include "CommandLine.h"
#include "FitChain.h"
#include "JecSysError.h"

int main(int argc, char** argv) {
  return runMain("jecMakeCard", [&]() {
    CommandLine cli("jecMakeCard", "jecMakeCard -e <epoch|all> [options]",
                    "write the datacard and the fit-ready shapes file of an epoch");
    cli.addOption("epoch", 'e', "name", "epoch from config/Inputs.json, or 'all' (reference IOVs first)");
    cli.addOption("config", 0, "dir", "configuration directory (default: config)");
    cli.addOption("inputs", 0, "file", "epoch registry (default: <config>/Inputs.json)");
    cli.setFooter("Output: input/forfit/card/<corLevel>/<epoch>/datacard.txt and shapes.root\n"
                  "Settings: config/MakeCard.json. The datacard is generated; do not edit it.");
    cli.parse(argc, argv);
    if (cli.helpRequested() || !cli.has("epoch")) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    const std::string configDir = cli.value("config", "config");
    const Inputs inputs(cli.value("inputs", configDir + "/Inputs.json"));
    const std::string epoch = cli.value("epoch");
    const std::vector<std::string> epochs =
        epoch == "all" ? inputs.epochsInDependencyOrder() : std::vector<std::string>{epoch};
    for (const std::string& name : epochs) fitChain::makeCard(inputs, name, configDir);
    return 0;
  });
}
