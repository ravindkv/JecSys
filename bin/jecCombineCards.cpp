// jecCombineCards: merge several datacards into one fit (the combineCards.py of
// combine). Channels get the prefix "<label>_"; nuisances
// with the same name in different cards become one, fully correlated nuisance.
#include <iostream>

#include "CommandLine.h"
#include "Datacard.h"
#include "JecSysError.h"
#include "TextTools.h"

int main(int argc, char** argv) {
  return runMain("jecCombineCards", [&]() {
    CommandLine cli("jecCombineCards", "jecCombineCards <label>=<datacard.txt> ... -o <out/datacard.txt>",
                    "combine datacards of several epochs or eta bins into one");
    cli.addOption("output", 'o', "file", "combined datacard to write (required)");
    cli.addOption("decorrelate", 0, "regex",
                  "nuisances to keep separate per card: renamed to <name>_<label>");
    cli.setFooter("Example:\n  jecCombineCards 2016BCDEF=input/forfit/card/L1L2L3Res/2016BCDEF/datacard.txt \\\n"
                  "                  2016GH=input/forfit/card/L1L2L3Res/2016GH/datacard.txt -o input/forfit/card/L1L2L3Res/Run2016/datacard.txt\n"
                  "Nuisances with the same name in several cards become ONE parameter. Use\n"
                  "--decorrelate (e.g. 'fsr_.*') for those that are independent in each card.\n"
                  "The model parameters are shared by all channels of the combined card.");
    cli.parse(argc, argv);
    if (cli.helpRequested() || cli.positional().empty() || !cli.has("output")) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    std::vector<std::pair<std::string, Datacard>> cards;
    for (const std::string& argument : cli.positional()) {
      const std::size_t equals = argument.find('=');
      if (equals == std::string::npos || equals == 0) {
        throw JecSysError("expected <label>=<datacard.txt>, got '" + argument + "'");
      }
      cards.emplace_back(argument.substr(0, equals), Datacard::read(argument.substr(equals + 1)));
    }
    const std::string output = cli.value("output");
    const Datacard combined = Datacard::combine(cards, textTools::dirName(output), cli.value("decorrelate"));
    combined.write(output);
    std::cout << "Wrote " << output << " (" << combined.channels.size() << " channels, "
              << combined.nuisances.size() << " nuisances)\n";
    for (const std::string& warning : combined.warnings()) std::cout << "  note: " << warning << "\n";
    return 0;
  });
}
