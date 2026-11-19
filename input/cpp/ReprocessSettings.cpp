#include "ReprocessSettings.h"

#include "InputStep.h"
#include "Inputs.h"
#include "JecSysError.h"
#include "TextTools.h"

namespace {

using Json = nlohmann::json;
using Rule = ReprocessSettings::Rule;
using Correction = ReprocessSettings::Correction;

std::set<std::string> stringSet(const SettingsReader& r, const std::string& key) {
  const auto list = r.get<std::vector<std::string>>(key);
  return {list.begin(), list.end()};
}

// A map of names in which null entries (not set for this era) are left out.
std::map<std::string, std::string> nameMap(const SettingsReader& r, const std::string& key) {
  const Json& object = r.get<Json>(key);
  if (!object.is_object()) throw JecSysError(r.where() + ": '" + key + "' must be an object");
  std::map<std::string, std::string> names;
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (it.value().is_null()) continue;
    if (!it.value().is_string()) throw JecSysError(r.where() + "." + key + ": '" + it.key() + "' must be a string");
    names[it.key()] = it.value().get<std::string>();
  }
  return names;
}

// A string that must be set (not null) for the epoch.
std::string required(const SettingsReader& r, const std::string& key) {
  if (r.json().contains(key) && r.json().at(key).is_null()) {
    throw JecSysError(r.where() + ": '" + key + "' is not set for this epoch (null)");
  }
  return r.get<std::string>(key);
}

std::vector<Rule> parseRules(const SettingsReader& r, const std::string& key, bool withRange) {
  std::vector<Rule> rules;
  int index = 0;
  for (const Json& entry : r.get<std::vector<Json>>(key)) {
    const std::string where = r.where() + "." + key + "[" + std::to_string(index++) + "]";
    const SettingsReader e(entry, where,
                           withRange ? std::set<std::string>{"samples", "parents", "types", "range"}
                                     : std::set<std::string>{"samples", "parents", "types", "op"});
    Rule rule;
    if (e.has("samples")) rule.samples = stringSet(e, "samples");
    if (e.has("parents")) rule.parents = stringSet(e, "parents");
    if (e.has("types")) rule.types = stringSet(e, "types");
    if (withRange) {
      const Json range = e.get<Json>("range");
      if (!range.is_array() || range.size() != 2 || !range[0].is_number() ||
          !(range[1].is_number() || range[1].is_null())) {
        throw JecSysError(where + ": range must be [min, max or null]");
      }
      rule.lo = range[0].get<double>();
      if (!range[1].is_null()) rule.hi = range[1].get<double>();
    } else {
      rule.op = e.get<std::string>("op");
      if (rule.op != "ratio" && rule.op != "diff") throw JecSysError(where + ": op must be ratio or diff");
    }
    rules.push_back(rule);
  }
  return rules;
}

Correction parseCorrection(const SettingsReader& corrections, const std::string& key) {
  const bool mass = key.size() > 4 && key.compare(key.size() - 4, 4, "Mass") == 0;
  std::set<std::string> keys = {"apply", "sample", "tiers", "types", "notRatioTypes"};
  if (mass) keys.insert({"ptFactor", "parameters", "range"});
  if (key == "gamScale") keys.insert("value");
  if (key == "multijetRecoilScale") keys.insert({"formula", "range", "parameters", "scale"});
  const SettingsReader r = corrections.child(key, keys);
  Correction c;
  c.apply = r.get<bool>("apply");
  c.sample = r.get<std::string>("sample");
  c.tiers = stringSet(r, "tiers");
  c.types = stringSet(r, "types");
  c.notRatioTypes = stringSet(r, "notRatioTypes");
  if (mass || key == "multijetRecoilScale") {
    std::tie(c.xmin, c.xmax) = r.range("range");
    if (c.apply) {
      if (r.json().at("parameters").is_null()) {
        throw JecSysError(r.where() + ": 'parameters' is not set for this epoch (null)");
      }
      c.parameters = r.get<std::vector<double>>("parameters");
    }
  }
  if (mass) c.ptFactor = r.get<double>("ptFactor");
  if (key == "gamScale") c.value = r.get<double>("value");
  if (key == "multijetRecoilScale") {
    c.formula = r.get<std::string>("formula");
    c.scale = r.get<double>("scale");
  }
  return c;
}

}  // namespace

bool ReprocessSettings::Rule::matches(const std::string& sample, const std::string& parent,
                                      const std::string& type) const {
  const bool sampleMatches = samples.count(sample) || (!parent.empty() && parents.count(parent));
  return sampleMatches && (!types || types->count(type));
}

ReprocessSettings ReprocessSettings::parse(const nlohmann::json& json, const std::string& where,
                                           const Inputs& inputs) {
  const SettingsReader r(
      json, where,
      {"verbose", "tiers", "etaBins", "types", "alphas", "fractions", "mpfComponents", "samples", "flavorSets",
       "products", "inputs", "names", "paths", "countsRebinX", "ratioFromDataMc", "zllMergePtMin", "cleaning",
       "hadwRatioErrorScale", "ratioErrorsFromDataMc", "multijetRatioWindow", "corrections", "snipe", "style",
       "massHistograms", "jec"});
  ReprocessSettings s;
  s.verbose = r.get<bool>("verbose");
  s.tiers = r.get<std::vector<std::string>>("tiers");
  for (const auto& bin : r.get<std::vector<std::vector<double>>>("etaBins")) {
    if (bin.size() != 2) throw JecSysError(where + ": etaBins must be pairs [eta1, eta2]");
    s.etaBins.emplace_back(bin[0], bin[1]);
  }
  s.types = r.get<std::vector<std::string>>("types");
  s.alphas = r.get<std::vector<double>>("alphas");
  s.fractions = stringSet(r, "fractions");
  s.mpfComponents = stringSet(r, "mpfComponents");
  s.samples = r.get<std::vector<std::string>>("samples");

  const Json& flavorSets = r.get<Json>("flavorSets");
  for (auto it = flavorSets.begin(); it != flavorSets.end(); ++it) {
    const SettingsReader f(it.value(), where + ".flavorSets." + it.key(), {"prefix", "letters"});
    s.flavorSets[it.key()] = {f.get<std::string>("prefix"), f.get<std::vector<std::string>>("letters")};
    for (const std::string& letter : s.flavorSets[it.key()].letters) {
      if (letter.size() != 1) throw JecSysError(f.where() + ": letters must be single characters");
    }
  }

  const Json& products = r.get<Json>("products");
  for (auto it = products.begin(); it != products.end(); ++it) {
    const SettingsReader p(it.value(), where + ".products." + it.key(), {"tiers", "etaBins", "types", "alphas"});
    Product product;
    product.tiers = stringSet(p, "tiers");
    product.types = stringSet(p, "types");
    for (int bin : p.get<std::vector<int>>("etaBins")) product.etaBins.insert(bin);
    for (double alpha : p.get<std::vector<double>>("alphas")) product.alphas.insert(alpha);
    s.products[it.key()] = product;
  }

  const SettingsReader in = r.child("inputs", {"multijet", "incjet", "incjetEra", "pfjet_data", "pfjet_mc", "hadw",
                                               "gamjet", "zeejet", "zmmjet", "zllDirectory", "zjet", "etaWeights",
                                               "etaWeightsObject"});
  const auto path = [&](const std::string& key) { return inputs.resolvePath(required(in, key)); };
  s.inputs = {path("multijet"), path("incjet"),  required(in, "incjetEra"), path("pfjet_data"),
              path("pfjet_mc"), path("hadw"),    path("gamjet"),            path("zeejet"),
              path("zmmjet"),   required(in, "zllDirectory"), path("zjet"), path("etaWeights"),
              required(in, "etaWeightsObject")};

  const SettingsReader names =
      r.child("names", {"multijet", "hadw", "gamjet", "zll", "zjet", "zjetTags", "zjetGens"});
  s.names = {nameMap(names, "multijet"), nameMap(names, "hadw"),     nameMap(names, "gamjet"), nameMap(names, "zll"),
             nameMap(names, "zjet"),     nameMap(names, "zjetTags"), nameMap(names, "zjetGens")};
  s.paths = nameMap(r, "paths");
  for (const char* key : {"pfjet", "pfjetRho", "incjet", "hadw", "multijet", "gamjetCounts", "gamjet", "gamjetFlavor",
                          "zll", "zjet", "zjetProfile", "zjetFlavor"}) {
    if (!s.paths.count(key)) throw JecSysError(where + ": paths needs '" + std::string(key) + "'");
  }
  s.countsRebinX = r.get<std::map<std::string, int>>("countsRebinX");
  s.ratioFromDataMc = parseRules(r, "ratioFromDataMc", false);
  s.zllMergePtMin = r.get<double>("zllMergePtMin");

  const SettingsReader cleaning = r.child("cleaning", {"maxError", "minEvents", "minEventsSamples", "ptRanges"});
  s.maxError = cleaning.get<double>("maxError");
  s.minEvents = cleaning.get<double>("minEvents");
  s.minEventsSamples = stringSet(cleaning, "minEventsSamples");
  s.ptRanges = parseRules(cleaning, "ptRanges", true);

  const Json& hadwScale = r.get<Json>("hadwRatioErrorScale");
  for (auto it = hadwScale.begin(); it != hadwScale.end(); ++it) {
    if (!it.value().is_null()) s.hadwRatioErrorScale[it.key()] = it.value().get<double>();
  }
  s.ratioErrorsFromDataMc = stringSet(r, "ratioErrorsFromDataMc");
  s.multijetRatioWindow = r.get<double>("multijetRatioWindow");

  const SettingsReader corrections = r.child(
      "corrections", {"massFormula", "zmmMass", "zeeMass", "gamMass", "gamScale", "multijetRecoilScale"});
  s.massFormula = corrections.get<std::string>("massFormula");
  s.zmmMass = parseCorrection(corrections, "zmmMass");
  s.zeeMass = parseCorrection(corrections, "zeeMass");
  s.gamMass = parseCorrection(corrections, "gamMass");
  s.gamScale = parseCorrection(corrections, "gamScale");
  s.multijetRecoilScale = parseCorrection(corrections, "multijetRecoilScale");

  const Json& snipe = r.get<Json>("snipe");
  for (auto it = snipe.begin(); it != snipe.end(); ++it) {
    for (const auto& pair : it.value().get<std::vector<std::vector<double>>>()) {
      if (pair.size() != 2) throw JecSysError(where + ".snipe." + it.key() + ": entries are [centre, half width]");
      s.snipe[it.key()].emplace_back(pair[0], pair[1]);
    }
  }

  const SettingsReader style = r.child("style", {"marker", "color", "markerSize", "frame"});
  const Json& marker = style.get<Json>("marker");
  for (auto it = marker.begin(); it != marker.end(); ++it) {
    for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
      s.marker[it.key()][jt.key()] =
          inputTools::rootConstant(jt.value().get<std::string>(), style.where() + ".marker." + it.key());
    }
  }
  for (const auto& [key, name] : style.get<std::map<std::string, std::string>>("color")) {
    s.color[key] = inputTools::rootConstant(name, style.where() + ".color");
  }
  s.markerSize = style.get<std::map<std::string, double>>("markerSize");
  const SettingsReader frame = style.child("frame", {"nbins", "range", "minimum", "maximum", "title"});
  s.frame.nbins = frame.get<int>("nbins");
  std::tie(s.frame.xmin, s.frame.xmax) = frame.range("range");
  s.frame.minimum = frame.get<double>("minimum");
  s.frame.maximum = frame.get<double>("maximum");
  s.frame.title = frame.get<std::string>("title");

  const SettingsReader mass = r.child("massHistograms", {"etaBin", "zll", "zjet2D"});
  s.massEtaBin = mass.get<std::string>("etaBin");
  s.massZll = mass.get<std::map<std::string, std::string>>("zll");
  s.massZjet2D = mass.get<std::map<std::string, std::vector<std::string>>>("zjet2D");
  for (const char* tier : {"ratio", "data", "mc"}) {
    if (!s.massZll.count(tier)) throw JecSysError(mass.where() + ": zll needs '" + std::string(tier) + "'");
  }
  for (const char* alpha : {"30", "100"}) {
    if (!s.massZjet2D.count(alpha)) throw JecSysError(mass.where() + ": zjet2D needs '" + std::string(alpha) + "'");
  }

  const SettingsReader jec =
      r.child("jec", {"directory", "algorithm", "jetArea", "rho", "inversionRange", "inversion", "ptBins", "runcl",
                      "correctors", "combined", "uncertainty", "histograms"});
  s.jecDirectory = inputs.resolvePath(jec.get<std::string>("directory"));
  s.jecAlgorithm = jec.get<std::string>("algorithm");
  s.jetArea = jec.get<double>("jetArea");
  const SettingsReader rho = jec.child("rho", {"average", "data", "mc"});
  s.rhoAverage = rho.get<double>("average");
  s.rhoData = rho.get<double>("data");
  s.rhoMc = rho.get<double>("mc");
  std::tie(s.inversionMin, s.inversionMax) = jec.range("inversionRange");
  s.inversion = jec.get<std::string>("inversion");
  if (s.inversion != "getx" && s.inversion != "memo" && s.inversion != "secant") {
    throw JecSysError(jec.where() + ": inversion must be getx, memo or secant");
  }
  s.ptBins = jec.get<std::vector<double>>("ptBins");
  const SettingsReader runcl = jec.child("runcl", {"ptRef", "absEtaMax", "labels"});
  s.runclPtRef = runcl.get<double>("ptRef");
  s.runclAbsEtaMax = runcl.get<double>("absEtaMax");
  s.runclLabels = runcl.get<std::vector<std::string>>("labels");
  if (s.runclLabels.size() != 4) throw JecSysError(runcl.where() + ": labels needs 4 entries");

  const Json& correctors = jec.get<Json>("correctors");
  for (auto it = correctors.begin(); it != correctors.end(); ++it) {
    if (it.value().is_null()) {
      throw JecSysError(jec.where() + ".correctors: '" + it.key() + "' is not set for this epoch (null)");
    }
    s.correctors[it.key()] = it.value().get<std::vector<std::string>>();
  }
  for (const char* key : {"jec", "mcjec", "run1", "old", "l1flat", "l1rcdt", "l1pt", "l1mc", "l1rcmc", "l1dt", "l1c",
                          "l1s", "l1rc", "l2c", "l2s", "rcdnom", "rcmnom", "rcdalt", "rcmalt", "l1dnomsf", "l1daltsf",
                          "l1mnomsf", "l1dnom", "l1mnom", "l1dalt", "l1malt"}) {
    if (!s.correctors.count(key)) throw JecSysError(jec.where() + ".correctors: '" + std::string(key) + "' is missing");
  }
  if (jec.json().at("combined").is_null()) throw JecSysError(jec.where() + ": 'combined' is not set for this epoch (null)");
  for (const Json& entry : jec.get<std::vector<Json>>("combined")) {
    const SettingsReader c(entry, jec.where() + ".combined", {"lumi", "corrector"});
    s.combined.push_back({c.get<std::vector<double>>("lumi"), c.get<std::vector<std::string>>("corrector")});
  }
  const SettingsReader unc = jec.child("uncertainty", {"reference", "file", "sources"});
  const SettingsReader reference = unc.child("reference", {"file", "source"});
  s.uncertaintyReference = {reference.get<std::string>("file"), reference.get<std::string>("source")};
  s.uncertaintyFile = required(unc, "file");
  s.uncertaintySources = unc.get<std::map<std::string, std::string>>("sources");
  for (const char* key : {"total", "ref", "pt", "hcal", "ecal", "glu", "uds", "cha", "bot", "zjt", "gjt", "qcd", "pu",
                          "noflv"}) {
    if (!s.uncertaintySources.count(key)) {
      throw JecSysError(unc.where() + ".sources: '" + std::string(key) + "' is missing");
    }
  }
  for (const Json& entry : jec.get<std::vector<Json>>("histograms")) {
    if (!entry.is_array() || entry.size() != 4) {
      throw JecSysError(jec.where() + ".histograms: entries are [key, name, title, fill or null]");
    }
    BandHistogram h{entry[0].get<std::string>(), entry[1].get<std::string>(), entry[2].get<std::string>(), {}};
    if (!entry[3].is_null()) {
      inputTools::rootConstant(entry[3].at(0).get<std::string>(), jec.where() + ".histograms");
      h.fill = std::make_pair(entry[3].at(0).get<std::string>(), entry[3].at(1).get<double>());
    }
    s.histograms.push_back(h);
  }
  return s;
}
