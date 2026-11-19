#include "GraphMaker.h"

#include <cmath>
#include <cstdio>

#include "GraphTools.h"
#include "InputStep.h"
#include "JecSysError.h"
#include "TDirectory.h"
#include "TF1.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TH2.h"
#include "TProfile.h"
#include "TextTools.h"

namespace {

using textTools::formatTemplate;
using textTools::TemplateValues;

// The points of a graph, copied (the graph may change while they are used).
struct Points {
  std::vector<double> x, y, ex, ey;
  explicit Points(const TGraphErrors& g) {
    for (int i = 0; i != g.GetN(); ++i) {
      x.push_back(g.GetX()[i]);
      y.push_back(g.GetY()[i]);
      ex.push_back(g.GetEX()[i]);
      ey.push_back(g.GetEY()[i]);
    }
  }
  int size() const { return static_cast<int>(x.size()); }
};

template <class Map>
int valueOr0(const Map& map, const std::string& key) {
  const auto it = map.find(key);
  return it == map.end() ? 0 : it->second;
}

int markerOf(const std::map<std::string, std::map<std::string, int>>& marker, const std::string& key,
             const std::string& type) {
  const auto it = marker.find(key);
  return it == marker.end() ? 0 : valueOr0(it->second, type);
}

}  // namespace

GraphMaker::GraphMaker(const ReprocessSettings& settings, const SampleTable& samples, std::string corLevel,
                       std::string epochName)
    : cfg_(settings), samples_(samples), corLevel_(std::move(corLevel)), epochName_(std::move(epochName)) {}

GraphMaker::~GraphMaker() = default;

TGraphErrors* GraphMaker::adopt(TGraphErrors* graph) {
  owned_.emplace_back(graph);
  return graph;
}

TGraphErrors* GraphMaker::graph(const Key& key) const {
  const auto it = graphs_.find(key);
  if (it == graphs_.end()) {
    throw JecSysError("reprocess: no graph " + std::get<1>(key) + "_" + std::get<2>(key) + " in tier " +
                      std::get<0>(key) + " (eta bin " + std::to_string(std::get<3>(key)) + ") to build from");
  }
  return it->second;
}

void GraphMaker::makeCorrectionFunctions() {
  const std::string where = "reprocess corrections";
  for (const auto& [key, spec] : {std::pair<const char*, const ReprocessSettings::Correction*>{"zeeMass", &cfg_.zeeMass},
                                  {"zmmMass", &cfg_.zmmMass}, {"gamMass", &cfg_.gamMass}}) {
    if (!spec->apply) continue;
    auto f1 = inputTools::makeFunction(std::string("f1") + key, cfg_.massFormula, spec->xmin, spec->xmax, where);
    f1->SetParameters(spec->parameters.data());
    functions_[key] = std::move(f1);
  }
  const ReprocessSettings::Correction& mjb = cfg_.multijetRecoilScale;
  if (mjb.apply) {
    auto f1 = inputTools::makeFunction("f1mjb", mjb.formula, mjb.xmin, mjb.xmax, where);
    f1->SetParameters(mjb.parameters.data());
    functions_["multijetRecoilScale"] = std::move(f1);
  }
  for (const auto& [key, f1] : functions_) {
    const std::size_t given = key == "multijetRecoilScale" ? mjb.parameters.size()
                              : key == "zeeMass"           ? cfg_.zeeMass.parameters.size()
                              : key == "zmmMass"           ? cfg_.zmmMass.parameters.size()
                                                           : cfg_.gamMass.parameters.size();
    if (static_cast<int>(given) != f1->GetNpar()) {
      throw JecSysError(where + ": " + key + " needs " + std::to_string(f1->GetNpar()) + " parameters");
    }
  }
}

void GraphMaker::makeAll(TDirectory& out) {
  const ReprocessSettings::Frame& frame = cfg_.frame;
  for (const std::string& tier : cfg_.tiers) {
    out.mkdir(tier.c_str());
    out.cd(tier.c_str());
    TDirectory* dout0 = out.GetDirectory(tier.c_str());
    for (std::size_t ieta = 0; ieta != cfg_.etaBins.size(); ++ieta) {
      const auto [eta1, eta2] = cfg_.etaBins[ieta];
      const std::string name0 = formatTemplate("eta{a:02.0f}-{b:02.0f}", {{"a", eta1 * 10.0}, {"b", eta2 * 10.0}});
      std::printf("%s\n", name0.c_str());
      std::fflush(stdout);
      dout0->mkdir(name0.c_str());
      dout0->cd(name0.c_str());
      TDirectory* dout = dout0->GetDirectory(name0.c_str());
      dout->mkdir("orig");
      dout->cd();
      auto* h = new TH1D("h", formatTemplate(frame.title, {{"tier", tier}}).c_str(), frame.nbins, frame.xmin,
                         frame.xmax);  // belongs to dout
      h->GetXaxis()->SetMoreLogLabels();
      h->GetXaxis()->SetNoExponent();
      h->SetMinimum(frame.minimum);
      h->SetMaximum(frame.maximum);
      h->Write();
      for (const std::string& type : cfg_.types) {
        for (const std::string& sample : samples_.names()) {
          if (!samples_.has(sample, tier, static_cast<int>(ieta), type)) continue;
          for (std::size_t ialpha = 0; ialpha != cfg_.alphas.size(); ++ialpha) {
            const double alpha = cfg_.alphas[ialpha];
            if (samples_.hasAlpha(sample, alpha)) {
              make(tier, static_cast<int>(ieta), eta1, eta2, type, sample, static_cast<int>(ialpha), alpha, *dout);
            }
          }
        }
      }
    }
  }
}

TDirectory& GraphMaker::inputFor(const std::string& sample, const std::string& tier) const {
  std::string key = sample;
  if (sample == "pfjet") {
    key = "pfjet_" + tier;
  } else if (!inputs_.count(sample)) {
    key = samples_.parent(sample);
  }
  const auto it = inputs_.find(key);
  if (it == inputs_.end()) throw JecSysError("reprocess: no input file for sample " + sample + " (" + tier + ")");
  return *it->second;
}

std::string GraphMaker::objectPath(const std::string& tier, double eta1, double eta2, const std::string& type,
                                   const std::string& sample, const std::string& parent, double alpha) const {
  const auto& names = cfg_.names;
  const auto& paths = cfg_.paths;
  const auto name = [&](const std::map<std::string, std::string>& map, const std::string& key) {
    const auto it = map.find(key);
    if (it == map.end()) {
      throw JecSysError("Reprocess.json (" + epochName_ + "): no input name for " + sample + " " + tier + " " + type);
    }
    return it->second;
  };
  TemplateValues values{{"tier", tier},          {"type", type},           {"sample", sample},
                        {"eta1", eta1},          {"eta2", eta2},           {"alpha100", 100.0 * alpha},
                        {"eta1x10", 10.0 * eta1}, {"eta2x10", 10.0 * eta2}, {"eta1x1001", 10.01 * eta1},
                        {"eta2x1001", 10.01 * eta2}, {"corLevel", corLevel_}};
  std::string pattern;
  if (sample == "pfjet") {
    pattern = paths.at(type == "rho" ? "pfjetRho" : "pfjet");
  } else if (sample == "incjet") {
    pattern = paths.at("incjet");
    values.emplace("incjetEra", cfg_.inputs.incjetEra);
  } else if (sample == "hadw") {
    pattern = paths.at("hadw");
    values.emplace("name", name(names.hadw, type));
  } else if (sample == "multijet") {
    pattern = paths.at("multijet");
    values.emplace("tierName", name(names.multijet, tier));
    values.emplace("name", name(names.multijet, tier + type));
  } else if (sample == "gamjet") {
    const auto& g = names.gamjet;
    if (type == "counts") {
      pattern = paths.at("gamjetCounts");
      values.emplace("mpfName", name(g, "mpfchs1"));
      values.emplace("tierSuffix", name(g, tier == "ratio" ? "data" : tier));
      values.emplace("name", name(g, "counts"));
    } else {
      pattern = paths.at("gamjet");
      values.emplace("name", name(g, type));
      values.emplace("tierSuffix", name(g, tier));
    }
  } else if (parent == "gamjet") {
    pattern = paths.at("gamjetFlavor");
  } else if (sample == "zeejet" || sample == "zmmjet") {
    pattern = paths.at("zll");
    values.emplace("tierName", name(names.zll, tier));
    values.emplace("name", name(names.zll, type));
  } else if (sample == "zjet") {
    pattern = paths.at(cfg_.fractions.count(type) || type == "rho" ? "zjetProfile" : "zjet");
    values.emplace("tierName", name(names.zjet, tier));
    values.emplace("name", name(names.zjet, type));
  } else if (parent == "zjet") {
    pattern = paths.at("zjetFlavor");
    const char gen = samples_.generator(sample);
    values.emplace("tierName", name(names.zjet, tier));
    values.emplace("name", name(names.zjet, type));
    values.emplace("tag", name(names.zjetTags, std::string(1, samples_.tag(sample))));
    values.emplace("gen", tier == "mc" && gen ? name(names.zjetGens, std::string(1, gen)) : std::string());
  } else {
    throw JecSysError("reprocess: no object path for sample " + sample);
  }
  return formatTemplate(pattern, values);
}

void GraphMaker::make(const std::string& tier, int ieta, double eta1, double eta2, const std::string& type,
                      const std::string& sample, int ialpha, double alpha, TDirectory& dout) {
  const std::string parent = samples_.parent(sample);
  const bool isFlavorMc = samples_.isFlavorMc(sample);
  const std::string name = formatTemplate("{type}_{sample}_a{alpha:1.0f}",
                                          {{"type", type}, {"sample", sample}, {"alpha", 100.0 * alpha}});
  const Key key{tier, type, sample, ieta, ialpha};

  // 1. the object: read, merged (zlljet) or built from data and MC (ratio tier)
  std::string op;
  if (tier == "ratio") {
    for (const ReprocessSettings::Rule& rule : cfg_.ratioFromDataMc) {
      if (rule.matches(sample, parent, type)) {
        op = rule.op;
        break;
      }
    }
  }
  TObject* obj = nullptr;
  if (!op.empty()) {
    const TGraphErrors& gd = *graph({"data", type, sample, ieta, ialpha});
    const TGraphErrors& gm = *graph({"mc", type, sample, ieta, ialpha});
    obj = adopt((op == "ratio" ? graphTools::ratioGraphs(gd, gm) : graphTools::diffGraphs(gd, gm)).release());
  } else if (sample == "zlljet") {
    TGraphErrors* merged = adopt(graphTools::mergeGraphs(*graph({tier, type, "zeejet", ieta, ialpha}),
                                                         *graph({tier, type, "zmmjet", ieta, ialpha}))
                                     .release());
    const Points p(*merged);
    for (int i = p.size() - 1; i != -1; --i) {
      if (p.x[i] < cfg_.zllMergePtMin) merged->RemovePoint(i);
    }
    obj = merged;
  } else {
    TDirectory& input = inputFor(sample, tier);
    const std::string path = objectPath(tier, eta1, eta2, type, sample, parent, alpha);
    obj = input.Get(path.c_str());
    if (!obj) {
      char buffer[128];
      std::snprintf(buffer, sizeof(buffer), "%g-%g", eta1, eta2);
      throw JecSysError(std::string(input.GetName()) + ": object '" + path + "' not found (" + sample + " " + type +
                        " " + tier + ", eta " + buffer + ")");
    }
    if (!obj->InheritsFrom(TH1::Class())) {  // histograms belong to their file, graphs to us
      if (!obj->InheritsFrom(TGraphErrors::Class())) {
        throw JecSysError("reprocess: " + name + " (" + tier + ") is a " + obj->ClassName() + ", not a TGraphErrors");
      }
      adopt(static_cast<TGraphErrors*>(obj));
    }
  }

  // 2. event counts: written as they are, used below to clean the graphs
  if (type == "counts") {
    const auto rebin = cfg_.countsRebinX.find(sample);
    if (rebin != cfg_.countsRebinX.end() && rebin->second && obj->InheritsFrom("TH2")) {
      dout.cd();
      TH2* h2 = static_cast<TH2*>(obj)->RebinX(rebin->second);  // in place, in the input file's memory
      obj = h2->ProjectionX(name.c_str());
    }
    if (!(obj->InheritsFrom("TH1D") || obj->InheritsFrom("TH1F"))) {
      throw JecSysError("reprocess: counts for " + sample + " " + tier + " are a " + obj->ClassName() +
                        ", not a TH1D/TH1F");
    }
    auto* counts = static_cast<TH1*>(obj);
    dout.cd();
    counts->SetName(name.c_str());
    counts->Write();
    counts_[{tier, sample, ieta, ialpha}] = counts;
    return;
  }

  // 3. histograms and profiles become graphs
  if (obj->InheritsFrom("TH2")) {
    obj = adopt(new TGraphErrors(static_cast<TH2*>(obj)->ProfileX()->ProjectionX()));
  }
  if (obj->InheritsFrom("TH1")) obj = adopt(new TGraphErrors(static_cast<TH1*>(obj)));
  if (!obj->InheritsFrom("TGraphErrors")) {
    throw JecSysError("reprocess: " + name + " (" + tier + ") is a " + obj->ClassName() + ", not a TGraphErrors");
  }
  TGraphErrors& g = *static_cast<TGraphErrors*>(obj);
  {
    const Points p(g);
    for (int i = p.size() - 1; i != -1; --i) {
      if (p.y[i] == 0 && p.ey[i] == 0) g.RemovePoint(i);
    }
  }
  TGraphErrors* gOrig = adopt(static_cast<TGraphErrors*>(g.Clone()));
  gOrig->SetName(name.c_str());

  // 4. cleaning: large errors, pT ranges, low event counts
  clean(g, tier, type, sample, parent, ieta, ialpha, isFlavorMc);

  // 5. ratio-tier uncertainties and multijet ratio from data and MC
  if (sample == "hadw" && tier == "ratio") {
    const auto k = cfg_.hadwRatioErrorScale.find(type);
    if (k != cfg_.hadwRatioErrorScale.end()) {
      const Points p(g);
      for (int i = 0; i != p.size(); ++i) g.SetPointError(i, p.ex[i], p.ey[i] * k->second);
    }
  }
  if (tier == "ratio" && cfg_.ratioErrorsFromDataMc.count(sample)) {
    ratioErrors(g, *graph({"data", type, sample, ieta, ialpha}), *graph({"mc", type, sample, ieta, ialpha}));
  }
  if (sample == "multijet" && tier == "ratio") {
    multijetRatio(g, type, *graph({"data", type, sample, ieta, ialpha}), *graph({"mc", type, sample, ieta, ialpha}));
  }

  // 6. corrections and single bins removed by hand
  applyCorrections(g, tier, type, sample);
  const auto snipes = cfg_.snipe.find(sample);
  if (snipes != cfg_.snipe.end() && !snipes->second.empty()) {
    const Points p(g);
    for (int i = p.size() - 1; i != -1; --i) {
      for (const auto& [centre, half] : snipes->second) {
        if (std::fabs(p.x[i] - centre) < half) {
          g.RemovePoint(i);
          break;
        }
      }
    }
  }

  // 7. write the uncleaned and the final graph
  dout.cd("orig");
  gOrig->Write();
  dout.cd();
  g.SetName(name.c_str());
  g.UseCurrentStyle();
  setStyle(g, tier, type, sample, parent, alpha);
  g.SetDrawOption("SAMEP");
  g.Write();
  graphs_[key] = &g;
}

void GraphMaker::setStyle(TGraphErrors& g, const std::string& tier, const std::string& type, const std::string& sample,
                          const std::string& parent, double alpha) const {
  const auto& marker = cfg_.marker;
  const auto& color = cfg_.color;
  const bool isFrac = cfg_.fractions.count(type) != 0;
  const bool isMpfComponent = cfg_.mpfComponents.count(type) != 0;
  g.SetMarkerStyle(markerOf(marker, sample, type));
  g.SetMarkerColor(valueOr0(color, sample));
  g.SetLineColor(valueOr0(color, sample));
  if (sample == "pfjet" || (isFrac && (sample == "zjet" || sample == "zlljet" || sample == "gamjet")) ||
      isMpfComponent) {
    const int mcMarker = markerOf(marker, sample + "_" + tier, type);
    g.SetMarkerStyle(tier == "mc" && mcMarker != 0 ? mcMarker : markerOf(marker, sample, type));
    const int specific = valueOr0(color, sample + "_" + type);
    const int col = specific ? specific : valueOr0(color, sample);
    g.SetMarkerColor(col);
    g.SetLineColor(col);
  }
  if (parent == "zjet" || parent == "gamjet") {
    g.SetMarkerStyle(markerOf(marker, parent, type));
    const int own = valueOr0(color, sample);
    const int col = own ? own : valueOr0(color, parent);
    g.SetMarkerColor(col);
    g.SetLineColor(col);
  }
  const std::string sizeKey = formatTemplate("{alpha:.2f}", {{"alpha", alpha}});
  const auto size = cfg_.markerSize.find(sizeKey);
  if (size == cfg_.markerSize.end()) throw JecSysError("Reprocess.json: no style.markerSize for alpha " + sizeKey);
  g.SetMarkerSize(size->second);
}

void GraphMaker::clean(TGraphErrors& g, const std::string& tier, const std::string& type, const std::string& sample,
                       const std::string& parent, int ieta, int ialpha, bool isFlavorMc) const {
  std::vector<const ReprocessSettings::Rule*> ranges;
  for (const ReprocessSettings::Rule& rule : cfg_.ptRanges) {
    if (rule.matches(sample, parent, type)) ranges.push_back(&rule);
  }
  const bool useCounts = cfg_.minEventsSamples.count(sample) != 0;
  TH1 *hcount = nullptr, *hmc = nullptr, *hdt = nullptr;
  if (useCounts) {
    const auto count = [&](const std::string& t) {
      const auto it = counts_.find({t, sample, ieta, ialpha});
      if (it == counts_.end()) {
        throw JecSysError("reprocess: no event counts for " + t + " " + sample + " (eta bin " + std::to_string(ieta) +
                          ", alpha " + std::to_string(ialpha) + ")");
      }
      return it->second;
    };
    hcount = count(tier);
    if (tier == "ratio") {
      hmc = count("mc");
      hdt = count("data");
    }
  }
  const Points p(g);
  for (int i = p.size() - 1; i != -1; --i) {
    const double pt = p.x[i];
    bool outside = false;
    for (const ReprocessSettings::Rule* rule : ranges) {
      if (pt < rule->lo || (rule->hi && pt > *rule->hi)) outside = true;
    }
    if (p.ey[i] > cfg_.maxError && !isFlavorMc) {
      g.RemovePoint(i);
    } else if (outside) {
      g.RemovePoint(i);
    } else if (useCounts) {
      if (tier == "ratio") {
        const double nmc = hmc->GetBinContent(hmc->FindBin(pt));
        const double ndt = hdt->GetBinContent(hdt->FindBin(pt));
        if (nmc < cfg_.minEvents || ndt < cfg_.minEvents) {
          if (cfg_.verbose) {
            std::printf(
                "ratio %s %s ieta %d ialpha %d ipt %d pt %g REMOVE POINT... nentries MC %g data %g y: %g+/-%g\n",
                sample.c_str(), type.c_str(), ieta, ialpha, hcount->FindBin(pt), pt, nmc, ndt, p.y[i], p.ey[i]);
          }
          g.RemovePoint(i);
        }
      } else {
        const double nentries = hcount->GetBinContent(hcount->FindBin(pt));
        if (nentries < cfg_.minEvents) {
          if (cfg_.verbose) {
            std::printf("%s %s %s ieta %d ialpha %d ipt %d pt %g REMOVE POINT... nentries %g y: %g+/-%g\n",
                        tier.c_str(), sample.c_str(), type.c_str(), ieta, ialpha, hcount->FindBin(pt), pt, nentries,
                        p.y[i], p.ey[i]);
          }
          g.RemovePoint(i);
        }
      }
    }
  }
}

void GraphMaker::ratioErrors(TGraphErrors& g, const TGraphErrors& gd, const TGraphErrors& gm) {
  const Points d(gd), m(gm), p(g);
  for (int i = 0; i != p.size(); ++i) {
    double exd = 0, eyd = 0, eym = 0;
    for (int j = 0; j != d.size(); ++j) {
      if (std::fabs(p.x[i] - d.x[j]) < p.ex[i]) {
        exd = d.ex[j];
        eyd = d.ey[j];
      }
    }
    for (int j = 0; j != m.size(); ++j) {
      if (std::fabs(p.x[i] - m.x[j]) < p.ex[i]) eym = m.ey[j];
    }
    g.SetPoint(i, p.x[i], p.y[i]);
    g.SetPointError(i, exd, std::sqrt(eyd * eyd + eym * eym));
  }
}

void GraphMaker::multijetRatio(TGraphErrors& g, const std::string& type, const TGraphErrors& gd,
                               const TGraphErrors& gm) const {
  const Points D(gd), M(gm);
  const Points p(g);
  for (int i = 0; i != p.size(); ++i) {
    const double ex = cfg_.multijetRatioWindow * p.x[i];
    double xd = 0, yd = 0, eyd = 0, xm = 0, ym = 0, eym = 0;
    for (int j = 0; j != D.size(); ++j) {
      if (std::fabs(p.x[i] - D.x[j]) < ex) xd = D.x[j], yd = D.y[j], eyd = D.ey[j];
    }
    for (int j = 0; j != M.size(); ++j) {
      if (std::fabs(p.x[i] - M.x[j]) < ex) xm = M.x[j], ym = M.y[j], eym = M.ey[j];
    }
    const bool both = yd != 0 && ym != 0;
    if (type == "crecoil") {
      g.SetPoint(i, 0.5 * (xd + xm), both ? 0.5 * (yd + ym) : 0);
      g.SetPointError(i, 0.5 * std::fabs(xd - xm),
                      both ? std::sqrt(0.5 * eyd * eyd + 0.5 * eym * eym + (yd - ym) * (yd - ym)) : 0);
    } else {
      g.SetPoint(i, 0.5 * (xd + xm), ym ? yd / ym : 0.0);
      g.SetPointError(i, 0.5 * std::fabs(xd - xm),
                      both ? yd / ym * std::sqrt((eyd / yd) * (eyd / yd) + (eym / ym) * (eym / ym)) : 0.0);
    }
  }
  const Points q(g);
  for (int i = q.size() - 1; i != -1; --i) {
    if (q.y[i] == 0 && q.ey[i] == 0) g.RemovePoint(i);
  }
}

void GraphMaker::applyCorrections(TGraphErrors& g, const std::string& tier, const std::string& type,
                                  const std::string& sample) const {
  const std::pair<const char*, const ReprocessSettings::Correction*> corrections[] = {
      {"zmmMass", &cfg_.zmmMass},
      {"zeeMass", &cfg_.zeeMass},
      {"gamMass", &cfg_.gamMass},
      {"gamScale", &cfg_.gamScale},
      {"multijetRecoilScale", &cfg_.multijetRecoilScale}};
  for (const auto& [key, spec] : corrections) {
    if (!(spec->apply && sample == spec->sample && spec->tiers.count(tier) && spec->types.count(type))) continue;
    if (tier == "ratio" && spec->notRatioTypes.count(type)) continue;
    const Points p(g);
    const std::string which(key);
    if (which == "gamScale") {
      for (int i = 0; i != p.size(); ++i) g.SetPoint(i, p.x[i], p.y[i] * spec->value);
    } else if (which == "multijetRecoilScale") {
      const TF1& f1 = *functions_.at(which);
      for (int i = 0; i != p.size(); ++i) g.SetPoint(i, p.x[i], p.y[i] / f1.Eval(p.x[i]) * spec->scale);
    } else {
      const TF1& f1 = *functions_.at(which);
      for (int i = 0; i != p.size(); ++i) g.SetPoint(i, p.x[i], p.y[i] * f1.Eval(spec->ptFactor * p.x[i]));
    }
  }
}
