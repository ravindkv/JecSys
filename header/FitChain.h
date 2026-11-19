#pragma once
// The three stages of the fit half of the package as functions, so that the
// executables (jecMakeCard, jecWorkspace, jecFit) and the driver (runFit) share one
// implementation: card -> workspace -> fit.
#include <string>

#include "Datacard.h"
#include "FitMethod.h"
#include "Inputs.h"

namespace fitChain {

// forfit file -> input/forfit/card/<corLevel>/<epoch>/{datacard.txt, shapes.root}
Datacard makeCard(const Inputs& inputs, const std::string& epochName, const std::string& configDir);

// datacard -> workspace. The model configuration is config/<model>.json with the era
// and epoch overrides of the card's epoch (defaults only for a combined card).
void makeWorkspace(const Inputs& inputs, const std::string& cardPath, const std::string& workspacePath,
                   const std::string& modelName, const std::string& configDir);

// workspace -> results of one method
void fit(const std::string& workspacePath, const FitOptions& options);

}  // namespace fitChain
