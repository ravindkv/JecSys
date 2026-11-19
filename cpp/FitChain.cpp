#include "FitChain.h"

#include <iostream>

#include "ConfigFile.h"
#include "JecSysError.h"
#include "MakeCard.h"
#include "Workspace.h"

namespace fitChain {

Datacard makeCard(const Inputs& inputs, const std::string& epochName, const std::string& configDir) {
  const Epoch& epoch = inputs.epoch(epochName);
  const ConfigFile file(configDir + "/MakeCard.json");
  const nlohmann::json resolved = file.resolve(epoch.era, epoch.name);
  const MakeCard makeCard(MakeCardConfig::fromJson(resolved, file.path()), inputs, resolved.dump(2));
  const Datacard card = makeCard.run(epoch);
  std::cout << "Wrote " << inputs.cardPath(epoch) << " and " << inputs.shapesPath(epoch) << " ("
            << card.channels.size() << " channels, " << card.nuisances.size() << " nuisances)\n";
  for (const std::string& warning : card.warnings()) std::cout << "  note: " << warning << "\n";
  return card;
}

void makeWorkspace(const Inputs& inputs, const std::string& cardPath, const std::string& workspacePath,
                   const std::string& modelName, const std::string& configDir) {
  const Datacard card = Datacard::read(cardPath);

  // Era of the card's epoch, if it is a single known epoch.
  std::string era;
  try {
    era = inputs.epoch(card.epoch).era;
  } catch (const JecSysError&) {
    std::cout << "  note: '" << card.epoch << "' is not an epoch of the registry; using the default "
              << "settings of " << modelName << "\n";
  }
  const ConfigFile modelFile(configDir + "/" + modelName + ".json");
  const std::string modelConfig = modelFile.resolve(era, card.epoch).dump(2);
  makePhysicsModel(modelName, modelConfig);  // fail now, not at fit time, if it is invalid

  const Workspace workspace = Workspace::fromCard(card, modelName, modelConfig);
  workspace.write(workspacePath, cardPath);
  std::cout << "Wrote " << workspacePath << " (model " << modelName << ")\n";
}

void fit(const std::string& workspacePath, const FitOptions& options) {
  runFitMethod(Workspace::read(workspacePath), options);
}

}  // namespace fitChain
