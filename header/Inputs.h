#pragma once
// Epoch registry, read from config/Inputs.json: the root directories, and for every
// epoch its era, reference IOV, correction level and the files the fit needs. Paths
// are relative to the repository root; a path in a configuration file can start with
// a root as "{prefit}/jec/..." (resolvePath). Output locations are derived here, so no
// other class builds a path by hand.
#include <map>
#include <string>
#include <vector>

struct ObjectRef {
  std::string file;
  std::string object;
};

struct Epoch {
  std::string name;      // "2016GH"
  std::string era;       // "UL16"; selects the era overrides in the config files
  std::string refIov;    // "2016BCDEF"; equal to `name` for a reference IOV
  std::string corLevel;  // "L1L2L3Res" or "L1L2Res"
  std::string forfit;    // file with the pre-fit graphs ({forfit}/jecdata<epoch>.root), resolved
  ObjectRef ptBinning;   // histogram whose binning is used for hjesfit and its eigenvectors (file resolved)

  bool isRefIov() const { return name == refIov; }
};

class Inputs {
 public:
  explicit Inputs(const std::string& path);

  const Epoch& epoch(const std::string& name) const;  // throws if unknown
  const std::vector<std::string>& epochNames() const { return order_; }
  // Epochs in an order that puts every reference IOV before the epochs that use it.
  std::vector<std::string> epochsInDependencyOrder() const;

  // The directories of "roots": card, postfit (outputs of the fit half), prefit (external
  // inputs), forfit (the pre-fit files) and protolab (../JERCProtoLab). Throws if unknown.
  const std::string& root(const std::string& name) const;
  // `path` with every "{root}" replaced by that root, e.g. "{prefit}/jec/X.txt" ->
  // "input/prefit/jec/X.txt". Throws JecSysError for an unknown root.
  std::string resolvePath(const std::string& path) const;

  std::string cardDir(const Epoch& epoch) const;     // input/forfit/card/<corLevel>/<epoch>
  std::string postfitDir(const Epoch& epoch) const;  // output/postfit/<corLevel>/<epoch>
  std::string cardPath(const Epoch& epoch) const { return cardDir(epoch) + "/datacard.txt"; }
  std::string shapesPath(const Epoch& epoch) const { return cardDir(epoch) + "/shapes.root"; }
  std::string workspacePath(const Epoch& epoch) const { return cardDir(epoch) + "/workspace.root"; }

 private:
  std::string path_;
  std::map<std::string, std::string> roots_;
  std::map<std::string, Epoch> epochs_;
  std::vector<std::string> order_;
};
