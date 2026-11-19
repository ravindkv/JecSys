#pragma once
// The settings of Reprocess (input/config/Reprocess.json), parsed once and checked at
// start-up: every key, type and per-era value that the step needs (a "null" left by the
// era/epoch overrides is an error naming the epoch). Paths are resolved with Inputs.
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

class Inputs;

struct ReprocessSettings {
  // A rule of a rule list (ratioFromDataMc, cleaning.ptRanges): it matches a sample whose
  // name is in `samples` or whose parent is in `parents`, and a type in `types` (all
  // types if absent).
  struct Rule {
    std::set<std::string> samples, parents;
    std::optional<std::set<std::string>> types;
    std::string op;                      // ratioFromDataMc: "ratio" or "diff"
    double lo = 0;                       // ptRanges: [lo, hi]; hi absent = no limit
    std::optional<double> hi;
    bool matches(const std::string& sample, const std::string& parent, const std::string& type) const;
  };
  struct FlavorSet {
    std::string prefix;
    std::vector<std::string> letters;
  };
  struct Product {
    std::set<std::string> tiers, types;
    std::set<int> etaBins;
    std::set<double> alphas;
  };
  struct InputFiles {
    std::string multijet, incjet, incjetEra, pfjetData, pfjetMc, hadw, gamjet, zeejet, zmmjet, zllDirectory, zjet,
        etaWeights, etaWeightsObject;
  };
  struct Names {
    // Object names inside the input files; a missing entry (null in the configuration)
    // is an error when it is needed.
    std::map<std::string, std::string> multijet, hadw, gamjet, zll, zjet, zjetTags, zjetGens;
  };
  struct Correction {
    bool apply = false;
    std::string sample;
    std::set<std::string> tiers, types, notRatioTypes;
    double ptFactor = 1;                   // mass corrections
    std::vector<double> parameters;        // mass corrections, multijetRecoilScale
    double xmin = 0, xmax = 0;
    double value = 1;                      // gamScale
    std::string formula;                   // multijetRecoilScale
    double scale = 1;                      // multijetRecoilScale
  };
  struct Frame {
    int nbins;
    double xmin, xmax, minimum, maximum;
    std::string title;
  };
  struct Uncertainty {
    std::string file, source;
  };
  struct CombinedJec {
    std::vector<double> lumis;
    std::vector<std::string> corrector;
  };
  struct BandHistogram {
    std::string key, name, title;
    std::optional<std::pair<std::string, double>> fill;  // ROOT colour name and alpha
  };

  bool verbose;
  std::vector<std::string> tiers, types, samples;
  std::set<std::string> fractions, mpfComponents;
  std::vector<std::pair<double, double>> etaBins;
  std::vector<double> alphas;
  std::map<std::string, FlavorSet> flavorSets;
  std::map<std::string, Product> products;
  InputFiles inputs;
  Names names;
  std::map<std::string, std::string> paths;
  std::map<std::string, int> countsRebinX;
  std::vector<Rule> ratioFromDataMc;
  double zllMergePtMin;
  double maxError, minEvents;
  std::set<std::string> minEventsSamples;
  std::vector<Rule> ptRanges;
  std::map<std::string, double> hadwRatioErrorScale;  // null entries are left out
  std::set<std::string> ratioErrorsFromDataMc;
  double multijetRatioWindow;
  std::string massFormula;
  Correction zmmMass, zeeMass, gamMass, gamScale, multijetRecoilScale;
  std::map<std::string, std::vector<std::pair<double, double>>> snipe;  // sample -> [centre, half width]
  std::map<std::string, std::map<std::string, int>> marker;           // ROOT constants resolved
  std::map<std::string, int> color;
  std::map<std::string, double> markerSize;                           // "0.10" -> size
  Frame frame;
  std::string massEtaBin;
  std::map<std::string, std::string> massZll;                         // tier -> name template
  std::map<std::string, std::vector<std::string>> massZjet2D;         // "30", "100" -> candidates
  // jec
  std::string jecDirectory, jecAlgorithm;
  double jetArea;
  double rhoAverage, rhoData, rhoMc;
  double inversionMin, inversionMax;
  std::string inversion;  // getx, memo or secant
  std::vector<double> ptBins;
  double runclPtRef, runclAbsEtaMax;
  std::vector<std::string> runclLabels;
  std::map<std::string, std::vector<std::string>> correctors;  // key -> [L1, L2, residual] tags
  std::vector<CombinedJec> combined;
  Uncertainty uncertaintyReference;
  std::string uncertaintyFile;
  std::map<std::string, std::string> uncertaintySources;
  std::vector<BandHistogram> histograms;

  static ReprocessSettings parse(const nlohmann::json& json, const std::string& where, const Inputs& inputs);
};
