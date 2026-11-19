#pragma once
// The input-creation chain of one epoch (was mk_reprocess.C): the steps of
// input/config/InputChain.json, run in order in one process on the pre-fit file. All
// steps are built (and their settings checked) in the constructor, before any file is
// touched.
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "InputStep.h"

class InputChain {
 public:
  struct Options {
    std::set<std::string> steps;  // run only these step names (empty: all)
    std::string snapshotDir;      // copy the file after every step to <dir>/<epoch>/<tag>.root
    bool verbose = false;
    std::string jecInversion;     // overrides jec.inversion of Reprocess.json if set
  };

  InputChain(const Inputs& inputs, const Epoch& epoch, const Options& options);

  // Runs the steps on `jecdataPath`; prints the time of every step.
  void run(const std::string& jecdataPath) const;

  // The step names of input/config/InputChain.json that this build can run.
  static const std::set<std::string>& knownSteps();

 private:
  struct Entry {
    std::string tag;
    std::unique_ptr<InputStep> step;
  };
  Epoch epoch_;
  Options options_;
  std::vector<Entry> steps_;
};
