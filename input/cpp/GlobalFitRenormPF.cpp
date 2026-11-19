#include "GlobalFitRenormPF.h"

#include <cmath>
#include <limits>

#include "GraphTools.h"
#include "JecSysError.h"
#include "TF1.h"
#include "TFile.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TROOT.h"
#include "TextTools.h"

namespace {

const char* kClass = "GlobalFitRenormPF";

using Settings = GlobalFitRenormPF::Settings;
using textTools::formatTemplate;
using textTools::TemplateValues;

void checkTiers(const std::vector<std::string>& tiers, const std::string& where) {
  for (const std::string& tier : tiers) {
    if (tier != "data" && tier != "mc" && tier != "ratio") {
      throw JecSysError(where + ": unknown tier '" + tier + "' (data, mc or ratio)");
    }
  }
}

// The first object of `templates` that exists in `file` (null if none).
TObject* first(TFile& file, const std::vector<std::string>& templates, const TemplateValues& values) {
  for (const std::string& pattern : templates) {
    if (TObject* object = file.Get(formatTemplate(pattern, values).c_str())) return object;
  }
  return nullptr;
}

// The running inverse-variance average of one combination: sum (mean and its error),
// sum2 (mean of the squares), sumw (sum of the weights). Attached to gROOT.
struct Sums {
  std::unique_ptr<TH1> sum, sum2, sumw;
};

// Creates the sums from `pattern` on first use (names <name>_sum, _sum2, _sumw) and takes
// the line and marker colour of the current set, as the macro does for every set.
void prepareSums(Sums& sums, const TH1& pattern, const std::string& name, const TAttLine& lineFrom,
                 const TAttMarker& markerFrom) {
  using Entry = std::pair<std::unique_ptr<TH1>*, const char*>;
  for (const Entry& entry : {Entry{&sums.sum, "_sum"}, Entry{&sums.sum2, "_sum2"}, Entry{&sums.sumw, "_sumw"}}) {
    if (!*entry.first) entry.first->reset(static_cast<TH1*>(pattern.Clone((name + entry.second).c_str())));
  }
  for (TH1* hist : {sums.sum.get(), sums.sum2.get(), sums.sumw.get()}) hist->SetLineColor(lineFrom.GetLineColor());
  for (TH1* hist : {sums.sum.get(), sums.sum2.get(), sums.sumw.get()}) {
    hist->SetMarkerColor(markerFrom.GetMarkerColor());
  }
}

// Adds one measurement (weight w) to the running average of bin i.
void combineBin(Sums& sums, int i, double w, double yw, double y2w, double eyw) {
  TH1& hs = *sums.sum;
  const double w0 = sums.sumw->GetBinContent(i);
  const double yw0 = hs.GetBinContent(i) * w0;
  const double y2w0 = hs.GetBinContent(i) * hs.GetBinContent(i) * w0;
  const double eyw0 = hs.GetBinError(i) * w0;
  hs.SetBinContent(i, (yw0 + yw) / (w0 + w));
  hs.SetBinError(i, (eyw0 + eyw) / (w0 + w));
  sums.sum2->SetBinContent(i, (y2w0 + y2w) / (w0 + w));
  sums.sumw->SetBinContent(i, w0 + w);
}

// hs2 -> mean with the RMS as error; hs3 -> mean with RMS (+) statistical error.
void rmsAndTotal(const TH1& hs, TH1& hs2, TH1& hs3) {
  for (int i = 1; i != hs.GetNbinsX() + 1; ++i) {
    const double y = hs.GetBinContent(i);
    const double y2 = hs2.GetBinContent(i);
    const double ey = hs.GetBinError(i);
    hs2.SetBinContent(i, y);
    hs2.SetBinError(i, std::sqrt(std::fabs(y2 - y * y)));
    hs3.SetBinContent(i, y);
    hs3.SetBinError(i, std::sqrt(std::fabs(y2 - y * y) + ey * ey));
  }
}

}  // namespace

Settings Settings::parse(const nlohmann::json& json, const std::string& where) {
  const SettingsReader r(json, where, {"etaDir", "tiers", "variables", "renormalize", "ptBins", "fitFunction",
                                       "fitOption", "precombinePF", "precombineHDM"});
  Settings s;
  s.etaDir = r.get<std::string>("etaDir");
  s.tiers = r.get<std::vector<std::string>>("tiers");
  checkTiers(s.tiers, where);
  s.variables = r.get<std::vector<std::string>>("variables");

  const SettingsReader ren = r.child("renormalize", {"sets", "inputs", "output"});
  s.renSets = ren.get<std::vector<std::string>>("sets");
  s.renInputs = ren.get<std::vector<std::string>>("inputs");
  s.renOutput = ren.get<std::string>("output");

  s.ptBins = r.get<std::vector<double>>("ptBins");
  if (s.ptBins.size() < 2) throw JecSysError(where + ": ptBins needs at least two edges");
  const SettingsReader fit = r.child("fitFunction", {"formula", "range"});
  s.fitFormula = fit.get<std::string>("formula");
  std::tie(s.fitMin, s.fitMax) = fit.range("range");
  s.fitOption = r.get<std::string>("fitOption");

  const SettingsReader pf = r.child("precombinePF", {"sets", "input", "minPt", "histogram", "function", "output"});
  s.pfSets = pf.get<std::vector<std::string>>("sets");
  s.pfInput = pf.get<std::string>("input");
  s.pfMinPt = pf.get<std::map<std::string, double>>("minPt");
  s.pfHistogram = pf.get<std::string>("histogram");
  s.pfFunction = pf.get<std::string>("function");
  s.pfOutput = pf.get<std::string>("output");

  const SettingsReader hdm = r.child("precombineHDM", {"sets", "multijet", "tiers", "inputs", "crecoil", "l2l3res",
                                                      "histogram", "function", "variants"});
  s.hdmSets = hdm.get<std::vector<std::string>>("sets");
  s.hdmMultijet = hdm.get<std::string>("multijet");
  s.hdmTiers = hdm.get<std::vector<std::string>>("tiers");
  checkTiers(s.hdmTiers, hdm.where());
  s.hdmInputs = hdm.get<std::vector<std::string>>("inputs");
  s.hdmCrecoil = hdm.get<std::string>("crecoil");
  s.hdmL2l3res = hdm.get<std::string>("l2l3res");
  s.hdmHistogram = hdm.get<std::string>("histogram");
  s.hdmFunction = hdm.get<std::string>("function");
  for (const auto& entry : hdm.get<std::vector<nlohmann::json>>("variants")) {
    const SettingsReader v(entry, hdm.where() + ".variants", {"addMJ", "scaleRes"});
    s.hdmVariants.push_back({v.get<bool>("addMJ"), v.get<bool>("scaleRes")});
  }
  return s;
}

GlobalFitRenormPF::GlobalFitRenormPF(const Epoch& epoch)
    : epoch_(epoch),
      settings_(Settings::parse(inputTools::loadSettings(kClass, epoch), inputTools::settingsWhere(kClass, epoch))),
      style_(inputTools::loadSettings("TdrStyle", epoch), inputTools::settingsWhere("TdrStyle", epoch)) {}

void GlobalFitRenormPF::run(const std::string& path) {
  inputTools::requireFile(path, name());
  style_.setAsInChain();
  renormalize(path);
  precombinePf(path);
  for (const Variant& variant : settings_.hdmVariants) precombineHdm(path, variant);
}

void GlobalFitRenormPF::renormalize(const std::string& path) const {
  const Settings& cfg = settings_;
  TDirectory* curdir = gDirectory;
  std::unique_ptr<TFile> fin = inputTools::openFile(path, "UPDATE");
  curdir->cd();

  // m[set][var or "sum" or var_ren][tier]; the graphs read from the file and the sums
  // belong to `owned`.
  std::vector<std::unique_ptr<TGraphErrors>> owned;
  std::map<std::string, std::map<std::string, std::map<std::string, TGraphErrors*>>> m;
  const auto keep = [&](std::unique_ptr<TGraphErrors> graph) {
    owned.push_back(std::move(graph));
    return owned.back().get();
  };

  // Read in data and calculate the sums chf+nef+nhf
  for (const std::string& tier : cfg.tiers) {
    for (const std::string& var : cfg.variables) {
      for (const std::string& s : cfg.renSets) {
        auto* g = dynamic_cast<TGraphErrors*>(
            first(*fin, cfg.renInputs, {{"tier", tier}, {"var", var}, {"set", s}, {"epoch", epoch_.name}}));
        if (!g) {
          throw JecSysError(path + ": none of " + textTools::join(cfg.renInputs, ", ") + " for " + tier + ", " + var +
                            ", " + s);
        }
        m[s][var][tier] = keep(std::unique_ptr<TGraphErrors>(g));
        TGraphErrors*& gs = m[s]["sum"][tier];
        gs = gs ? keep(graphTools::diffGraphs(*g, *gs, 1.0, -1.0))
                : keep(std::unique_ptr<TGraphErrors>(
                      static_cast<TGraphErrors*>(g->Clone((std::string(g->GetName()) + "_sum").c_str()))));
      }
    }
  }

  // Renormalise data and MC, recalculate the data-MC difference
  for (const std::string& tier : cfg.tiers) {
    for (const std::string& s : cfg.renSets) {
      for (const std::string& var : cfg.variables) {
        auto& ren = m[s][var + "_ren"];
        if (tier == "data" || tier == "mc") {
          ren[tier] = keep(graphTools::ratioGraphs(*m[s][var][tier], *m[s]["sum"][tier]));
        } else {
          if (!ren.count("data") || !ren.count("mc")) {
            throw JecSysError(inputTools::settingsWhere(kClass, epoch_) + ": tier ratio needs data and mc before it");
          }
          ren[tier] = keep(graphTools::diffGraphs(*ren["data"], *ren["mc"]));
        }
      }
    }
  }

  // Write out results
  for (const std::string& tier : cfg.tiers) {
    for (const std::string& var : cfg.variables) {
      for (const std::string& s : cfg.renSets) {
        TGraphErrors* g = m[s][var + "_ren"][tier];
        fin->cd((tier + "/" + cfg.etaDir).c_str());
        const std::string name = formatTemplate(cfg.renOutput, {{"var", var}, {"set", s}, {"epoch", epoch_.name}});
        g->SetName(name.c_str());
        g->Write(name.c_str(), TObject::kOverwrite);
      }
    }
  }
  fin->Write();
  fin->Close();
  curdir->cd();
}

void GlobalFitRenormPF::precombinePf(const std::string& path) const {
  const Settings& cfg = settings_;
  const std::string where = inputTools::settingsWhere(kClass, epoch_);
  TDirectory* curdir = gDirectory;
  std::unique_ptr<TFile> fin = inputTools::openFile(path, "UPDATE");
  curdir->cd();
  const std::unique_ptr<TH1D> h(new TH1D(formatTemplate(cfg.pfHistogram, {{"epoch", epoch_.name}}).c_str(), "",
                                         static_cast<int>(cfg.ptBins.size()) - 1, cfg.ptBins.data()));
  const std::unique_ptr<TF1> f1 = inputTools::makeFunction(formatTemplate(cfg.pfFunction, {{"epoch", epoch_.name}}),
                                                           cfg.fitFormula, cfg.fitMin, cfg.fitMax, where);

  std::vector<std::unique_ptr<TGraphErrors>> graphs;
  std::map<std::pair<std::string, std::string>, Sums> sums;  // (var, tier)
  for (const std::string& tier : cfg.tiers) {
    for (const std::string& var : cfg.variables) {
      for (const std::string& s : cfg.pfSets) {
        const TemplateValues values{{"tier", tier}, {"var", var}, {"set", s}, {"epoch", epoch_.name}};
        auto* g = dynamic_cast<TGraphErrors*>(fin->Get(formatTemplate(cfg.pfInput, values).c_str()));
        if (!g) {
          throw JecSysError(path + ": missing " + formatTemplate(cfg.pfInput, values) + " (run renormalize first)");
        }
        graphs.emplace_back(g);
        Sums& store = sums[{var, tier}];
        prepareSums(store, *h, g->GetName(), *g, *g);
        const double minPt = cfg.pfMinPt.count(s) ? cfg.pfMinPt.at(s) : -std::numeric_limits<double>::infinity();
        for (int i = 1; i != store.sum->GetNbinsX() + 1; ++i) {
          const double ptmin = store.sum->GetBinLowEdge(i);
          const double ptmax = store.sum->GetBinLowEdge(i + 1);
          f1->SetRange(ptmin, ptmax);
          f1->SetParameter(0, 0);
          f1->SetParError(0, 0);
          g->Fit(f1.get(), cfg.fitOption.c_str());
          if (f1->GetParError(0) > 0 && !(ptmin < minPt)) {
            const double w = 1.0 / (f1->GetParError(0) * f1->GetParError(0));
            const double yw = f1->GetParameter(0) * w;
            const double y2w = f1->GetParameter(0) * f1->GetParameter(0) * w;
            const double eyw = f1->GetParError(0) * w;
            combineBin(store, i, w, yw, y2w, eyw);
          }
        }
      }
    }
  }

  std::vector<std::unique_ptr<TObject>> outputs;
  for (const std::string& tier : cfg.tiers) {
    for (const std::string& var : cfg.variables) {
      Sums& store = sums.at({var, tier});
      TH1* hs = store.sum.get();
      TH1* hs2 = store.sum2.get();
      curdir->cd();
      auto* hs3 = static_cast<TH1*>(hs2->Clone((std::string(hs2->GetName()) + "t").c_str()));
      outputs.emplace_back(hs3);
      hs3->SetLineColor(hs2->GetLineColor());
      hs3->SetMarkerColor(hs2->GetMarkerColor());
      rmsAndTotal(*hs, *hs2, *hs3);
      auto* g = new TGraphErrors(hs3);
      outputs.emplace_back(g);
      g->SetLineColor(hs3->GetLineColor());
      g->SetMarkerColor(hs3->GetMarkerColor());
      for (int i = g->GetN() - 1; i != -1; --i) {
        if (g->GetEY()[i] == 0) g->RemovePoint(i);
      }
      fin->cd((tier + "/" + cfg.etaDir).c_str());
      const std::string name = formatTemplate(cfg.pfOutput, {{"var", var}, {"epoch", epoch_.name}});
      using Output = std::pair<TNamed*, const char*>;
      for (const Output& output : {Output{g, ""}, Output{hs, "_stat"}, Output{hs2, "_syst"}, Output{hs3, "_tot"}}) {
        output.first->SetName((name + output.second).c_str());
        output.first->Write((name + output.second).c_str(), TObject::kOverwrite);
      }
      curdir->cd();
    }
  }
  fin->Write();
  fin->Close();
  curdir->cd();
}

void GlobalFitRenormPF::precombineHdm(const std::string& path, const Variant& variant) const {
  const Settings& cfg = settings_;
  const std::string where = inputTools::settingsWhere(kClass, epoch_);
  TDirectory* curdir = gDirectory;
  std::unique_ptr<TFile> fin = inputTools::openFile(path, "UPDATE");
  curdir->cd();
  std::vector<std::string> sets = cfg.hdmSets;
  if (variant.addMultijet) sets.push_back(cfg.hdmMultijet);
  const std::unique_ptr<TH1D> h1(new TH1D(formatTemplate(cfg.hdmHistogram, {{"epoch", epoch_.name}}).c_str(), "",
                                          static_cast<int>(cfg.ptBins.size()) - 1, cfg.ptBins.data()));
  const std::unique_ptr<TF1> f1 = inputTools::makeFunction(formatTemplate(cfg.hdmFunction, {{"epoch", epoch_.name}}),
                                                           cfg.fitFormula, cfg.fitMin, cfg.fitMax, where);

  // The histograms read from the file stay attached to it, so that fin->Write() writes
  // them again, as in the macro.
  std::map<std::string, Sums> sums;  // tier
  for (const std::string& tier : cfg.hdmTiers) {
    for (const std::string& s : sets) {
      auto* h = dynamic_cast<TH1*>(first(*fin, cfg.hdmInputs, {{"tier", tier}, {"set", s}, {"epoch", epoch_.name}}));
      if (!h) throw JecSysError(path + ": missing " + tier + "/hdm[_mpfchs1]_" + s);
      Sums& store = sums[tier];
      prepareSums(store, *h1, h->GetName(), *h, *h);
      TH1& hs = *store.sum;
      for (int i = 1; i != hs.GetNbinsX() + 1; ++i) {
        const double ptmin = hs.GetBinLowEdge(i);
        const double ptmax = hs.GetBinLowEdge(i + 1);
        f1->SetRange(ptmin, ptmax);
        f1->SetParameter(0, 0);
        f1->SetParError(0, 0);
        h->Fit(f1.get(), cfg.fitOption.c_str());
        if (f1->GetParError(0) > 0) {
          double w = 1.0 / (f1->GetParError(0) * f1->GetParError(0));
          double yw = f1->GetParameter(0) * w;
          double y2w = f1->GetParameter(0) * f1->GetParameter(0) * w;
          double eyw = f1->GetParError(0) * w;
          if (s == cfg.hdmMultijet) {
            // multijet balances against the recoil, so it is referenced to the
            // combination at the recoil pT of the sets before it
            const std::string crecoil = formatTemplate(cfg.hdmCrecoil, {{"tier", tier}, {"set", s}, {"epoch", epoch_.name}});
            const std::unique_ptr<TGraphErrors> gc(dynamic_cast<TGraphErrors*>(fin->Get(crecoil.c_str())));
            if (!gc) throw JecSysError(path + ": missing " + crecoil);
            const double ptlead = hs.GetBinCenter(i);
            const double ptrec = gc->Eval(ptlead) * ptlead;
            const int j = hs.FindBin(ptrec);
            const double yref = hs.GetBinContent(j);
            const double eyref = hs.GetBinError(j);
            const double eyRel = f1->GetParError(0) * yref;
            const double ey = std::sqrt(eyRel * eyRel + eyref * eyref);
            w = 1.0 / (ey * ey);
            yw = f1->GetParameter(0) * yref * w;
            const double yScaled = f1->GetParameter(0) * yref;
            y2w = yScaled * yScaled * w;
            eyw = ey * w;
          }
          combineBin(store, i, w, yw, y2w, eyw);
        }
      }
    }
  }

  std::vector<std::unique_ptr<TH1>> outputs;
  for (const std::string& tier : cfg.hdmTiers) {
    TH1* hs = sums.at(tier).sum.get();
    TH1* hs2 = sums.at(tier).sum2.get();
    curdir->cd();
    if (variant.scaleRes) {
      auto* hc = dynamic_cast<TH1*>(fin->Get(cfg.hdmL2l3res.c_str()));  // attached to the file
      if (!hc) throw JecSysError(path + ": missing " + cfg.hdmL2l3res);
      for (int i = 1; i != hs->GetNbinsX() + 1; ++i) {
        const double pt = hs->GetBinCenter(i);
        const int j = hc->FindBin(pt);
        hs->SetBinContent(i, hs->GetBinContent(i) * hc->GetBinContent(j));
        hs2->SetBinContent(i, hs2->GetBinContent(i) * (hc->GetBinContent(j) * hc->GetBinContent(j)));
      }
    }
    outputs.emplace_back(static_cast<TH1*>(hs2->Clone((std::string(hs2->GetName()) + "_tot").c_str())));
    TH1* hs3 = outputs.back().get();
    hs3->SetLineColor(hs2->GetLineColor());
    hs3->SetMarkerColor(hs2->GetMarkerColor());
    rmsAndTotal(*hs, *hs2, *hs3);
    fin->cd((tier + "/" + cfg.etaDir).c_str());
    const std::string name =
        std::string("hdm_cmb") + (variant.addMultijet ? "_mj" : "") + (variant.scaleRes ? "_res" : "");
    using Output = std::pair<TH1*, const char*>;
    for (const Output& output : {Output{hs, ""}, Output{hs2, "_syst"}, Output{hs3, "_tot"}}) {
      output.first->SetName((name + output.second).c_str());
      output.first->Write((name + output.second).c_str(), TObject::kOverwrite);
    }
    curdir->cd();
  }
  fin->Write();
  fin->Close();
  curdir->cd();
}
