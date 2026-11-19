// jecFit: run a fit method on a workspace (the "combine" of combine).
#include <iostream>

#include "CommandLine.h"
#include "FitChain.h"
#include "FitCommandLine.h"

int main(int argc, char** argv) {
  return runMain("jecFit", [&]() {
    CommandLine cli("jecFit", "jecFit <workspace.root> [-M <method>] [options]",
                    "fit a workspace and write the results");
    fitCommandLine::addOptions(cli);
    cli.setFooter(
        "Methods:\n"
        "  FitDiagnostics  best fit, covariance, eigenvectors, post-fit graphs -> fit.root, result.json\n"
        "  GoodnessOfFit   chi2/ndf per channel; with -t N a p-value from toys   -> gof.json\n"
        "  Impacts         effect of each nuisance on the JES and the parameters -> impacts.json\n"
        "  MultiDimFit     chi2 scan of one parameter (-P)                       -> scan_<par>.root/.json\n"
        "Examples:\n"
        "  jecFit input/forfit/card/L1L2L3Res/2016GH/workspace.root\n"
        "  jecFit input/forfit/card/L1L2L3Res/2016GH/workspace.root --mask '.*_hadw' -n noHadW\n"
        "  jecFit input/forfit/card/L1L2L3Res/2016GH/workspace.root --setParameters p8Flv=0 --freezeParameters p8Flv -n noFlv\n"
        "  jecFit input/forfit/card/L1L2L3Res/2016GH/workspace.root -M MultiDimFit -P p0Trk");
    cli.parse(argc, argv);
    if (cli.helpRequested() || cli.positional().size() != 1) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    fitChain::fit(cli.positional()[0], fitCommandLine::parse(cli));
    return 0;
  });
}
