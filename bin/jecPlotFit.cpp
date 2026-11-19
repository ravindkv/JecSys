// jecPlotFit: plots from a fit.root, without refitting.
#include <iostream>

#include "CommandLine.h"
#include "FitPlotter.h"

int main(int argc, char** argv) {
  return runMain("jecPlotFit", [&]() {
    CommandLine cli("jecPlotFit", "jecPlotFit <fit.root> [-o <dir>]",
                    "plot a fit result written by jecFit -M FitDiagnostics");
    cli.addOption("outDir", 'o', "dir", "output directory (default: next to the fit file)");
    cli.setFooter("Writes <fit>_prefit.pdf, <fit>_postfit.pdf, <fit>_pf.pdf and <fit>_pulls.pdf.");
    cli.parse(argc, argv);
    if (cli.helpRequested() || cli.positional().size() != 1) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    FitPlotter::run(cli.positional()[0], cli.value("outDir"));
    return 0;
  });
}
