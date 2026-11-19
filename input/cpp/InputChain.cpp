#include "InputChain.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "GlobalFitRenormPF.h"
#include "GlobalFitSyst.h"
#include "JecSysError.h"
#include "Reprocess.h"
#include "SoftRad.h"
#include "TH1.h"
#include "TROOT.h"
#include "TextTools.h"

namespace {

const char* kClass = "InputChain";

std::unique_ptr<InputStep> makeStep(const std::string& step, const nlohmann::json& arguments, const Inputs& inputs,
                                    const Epoch& epoch, const InputChain::Options& options, const std::string& where) {
  const auto noArguments = [&]() { ConfigFile::checkKeys(arguments, {}, where); };
  if (step == "reprocess") {
    noArguments();
    return std::make_unique<Reprocess>(inputs, epoch, options.jecInversion);
  }
  if (step == "softRad") return std::make_unique<SoftRad>(epoch, arguments, options.verbose);
  if (step == "globalFitSyst") {
    noArguments();
    return std::make_unique<GlobalFitSyst>(inputs, epoch);
  }
  if (step == "globalFitRenormPF") {
    noArguments();
    return std::make_unique<GlobalFitRenormPF>(epoch);
  }
  throw JecSysError(where + ": unknown step '" + step + "'");
}

}  // namespace

const std::set<std::string>& InputChain::knownSteps() {
  static const std::set<std::string> steps = {"reprocess", "softRad", "globalFitSyst", "globalFitRenormPF"};
  return steps;
}

InputChain::InputChain(const Inputs& inputs, const Epoch& epoch, const Options& options)
    : epoch_(epoch), options_(options) {
  for (const std::string& step : options.steps) {
    if (!knownSteps().count(step)) {
      throw JecSysError("unknown step '" + step + "' (known: " +
                        textTools::join({knownSteps().begin(), knownSteps().end()}, ", ") + ")");
    }
  }
  const std::string where = inputTools::settingsWhere(kClass, epoch);
  const SettingsReader settings(inputTools::loadSettings(kClass, epoch), where, {"steps"});
  int index = 0;
  for (const auto& entry : settings.get<std::vector<nlohmann::json>>("steps")) {
    const std::string at = where + ": steps[" + std::to_string(index++) + "]";
    const std::string step = ConfigFile::get<std::string>(entry, "step", at);
    const std::string tag = ConfigFile::get<std::string>(entry, "tag", at);
    if (!knownSteps().count(step)) throw JecSysError(at + ": unknown step '" + step + "'");
    if (!options.steps.empty() && !options.steps.count(step)) continue;
    nlohmann::json arguments = entry;
    arguments.erase("step");
    arguments.erase("tag");
    steps_.push_back({tag, makeStep(step, arguments, inputs, epoch, options, at)});
  }
  if (steps_.empty()) throw JecSysError(where + ": no step selected");
}

void InputChain::run(const std::string& path) const {
  // The steps rely on ROOT's default (InputStep.h); runMain() turns it off for the fit.
  TH1::AddDirectory(true);
  gROOT->cd();
  for (const Entry& entry : steps_) {
    const auto wallStart = std::chrono::steady_clock::now();
    const std::clock_t cpuStart = std::clock();
    entry.step->run(path);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wallStart).count();
    const double cpu = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    std::printf("jecMakeInputs: %s %-18s real %6.2f s  cpu %6.2f s\n", epoch_.name.c_str(), entry.step->name().c_str(),
                wall, cpu);
    std::fflush(stdout);
    if (!options_.snapshotDir.empty()) {
      const std::string dir = options_.snapshotDir + "/" + epoch_.name;
      textTools::makeDirs(dir);
      std::filesystem::copy_file(path, dir + "/" + entry.tag + ".root",
                                 std::filesystem::copy_options::overwrite_existing);
    }
  }
}
