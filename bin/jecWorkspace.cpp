// jecWorkspace: datacard -> workspace.root, a frozen, self-contained fit problem
// (the text2workspace of combine).
#include <iostream>

#include "CommandLine.h"
#include "FitChain.h"
#include "PhysicsModel.h"
#include "TextTools.h"

int main(int argc, char** argv) {
  return runMain("jecWorkspace", [&]() {
    CommandLine cli("jecWorkspace", "jecWorkspace <datacard.txt> [-P <model>] [-o <workspace.root>]",
                    "turn a datacard into a workspace");
    cli.addOption("physicsModel", 'P', "model",
                  "physics model (default: JesModel9; known: " + textTools::join(physicsModelNames(), ", ") + ")");
    cli.addOption("output", 'o', "file", "output file (default: workspace.root next to the datacard)");
    cli.addOption("config", 0, "dir", "configuration directory (default: config)");
    cli.addOption("inputs", 0, "file", "epoch registry (default: <config>/Inputs.json)");
    cli.parse(argc, argv);
    if (cli.helpRequested() || cli.positional().size() != 1) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    const std::string cardPath = cli.positional()[0];
    const std::string output = cli.value("output", textTools::dirName(cardPath) + "/workspace.root");
    const std::string configDir = cli.value("config", "config");
    const Inputs inputs(cli.value("inputs", configDir + "/Inputs.json"));
    fitChain::makeWorkspace(inputs, cardPath, output, cli.value("physicsModel", "JesModel9"), configDir);
    return 0;
  });
}
