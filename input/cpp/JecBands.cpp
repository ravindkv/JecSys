#include "JecBands.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "CondFormats/JetMETObjects/interface/FactorizedJetCorrector.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectionUncertainty.h"
#include "InputStep.h"
#include "JecSysError.h"
#include "TDirectory.h"
#include "TH1D.h"
#include "TH2.h"
#include "TextTools.h"

// The correctors and uncertainty sources of one run.
struct JecBands::Correctors {
  std::map<std::string, FactorizedJetCorrector*> cor;               // jec.correctors
  std::vector<std::pair<double, FactorizedJetCorrector*>> combined;  // luminosity weight, corrector
  std::map<std::string, JetCorrectionUncertainty*> unc;              // jec.uncertainty.sources
  JetCorrectionUncertainty* uncRef1 = nullptr;                       // jec.uncertainty.reference
  FactorizedJetCorrector& at(const std::string& key) const { return *cor.at(key); }
};

JecBands::JecBands(const ReprocessSettings& settings, std::string corLevel, std::string epochName,
                   JecInverter::Mode mode)
    : cfg_(settings), corLevel_(std::move(corLevel)), epochName_(std::move(epochName)), mode_(mode) {}

void JecBands::make(TDirectory& out, TH2& h2) {
  if (cfg_.verbose) std::printf("Instantiating JECs...\n");
  CorrectorCache cache(cfg_.jecDirectory, cfg_.jecAlgorithm, cfg_.verbose);
  Correctors c;
  for (const auto& [key, tags] : cfg_.correctors) c.cor[key] = &cache.corrector(tags);
  double lumtot = 0;
  for (const auto& entry : cfg_.combined) {
    for (double lumi : entry.lumis) lumtot += lumi;
  }
  double weightSum = 0;
  for (const auto& entry : cfg_.combined) {
    double group = 0;
    for (double lumi : entry.lumis) group += lumi;
    c.combined.emplace_back(group / lumtot, &cache.corrector(entry.corrector));
    weightSum += group / lumtot;
  }
  if (std::fabs(weightSum - 1) >= 1e-4) {
    throw JecSysError("Reprocess.json (" + epochName_ + "): jec.combined weights do not add up to 1");
  }
  if (cfg_.verbose) std::printf("Loading uncertainty sources...\n");
  c.uncRef1 = &cache.uncertainty(cfg_.uncertaintyReference.file, cfg_.uncertaintyReference.source);
  for (const auto& [key, source] : cfg_.uncertaintySources) c.unc[key] = &cache.uncertainty(cfg_.uncertaintyFile, source);

  JecInverter getJec(cfg_.jetArea, cfg_.inversionMin, cfg_.inversionMax, mode_);
  const int npt = static_cast<int>(cfg_.ptBins.size()) - 1;
  TAxis* xaxis = h2.GetXaxis();

  for (const auto& [eta1, eta2] : cfg_.etaBins) {
    if (!out.cd("ratio")) throw JecSysError("reprocess: no ratio directory in the output");
    TDirectory* dout1 = out.GetDirectory("ratio");
    const std::string name1 =
        textTools::formatTemplate("eta{a:02.0f}-{b:02.0f}", {{"a", eta1 * 10.0}, {"b", eta2 * 10.0}});
    dout1->cd(name1.c_str());
    TDirectory* dout2 = dout1->GetDirectory(name1.c_str());
    dout2->cd();
    std::printf("Eta bin:%g-%g\n", eta1, eta2);
    std::fflush(stdout);
    // All belong to dout2.
    std::map<std::string, TH1D*> hists;
    for (const auto& spec : cfg_.histograms) {
      hists[spec.key] = new TH1D(spec.name.c_str(), spec.title.c_str(), npt, cfg_.ptBins.data());
    }
    auto* hruncl = new TH1D("hruncl", ";mode;Runcl", 4, 0.5, 4.5);

    // reference R_uncl (unclustered response) at ptRef
    if (corLevel_ == "L1L2L3Res") {
      const double ptref = cfg_.runclPtRef;
      const int jref = std::max(1, std::min(xaxis->FindBin(ptref), xaxis->GetNbins()));
      TH1D* hpteta = h2.ProjectionY("hpteta", jref, jref);
      double suml2l3 = 0, suml2l3res = 0, sumw5 = 0, sumw8 = 0;
      for (int ieta = 1; ieta != hpteta->GetNbinsX() + 1; ++ieta) {
        const double abseta = hpteta->GetBinCenter(ieta), w = hpteta->GetBinContent(ieta);
        sumw8 += w;
        if (abseta < cfg_.runclAbsEtaMax) {
          sumw5 += w;
          const double rho = cfg_.rhoAverage;
          const double l2l3 = 0.5 * (1.0 / getJec(c.at("mcjec"), +abseta, ptref, rho) +
                                     1.0 / getJec(c.at("mcjec"), -abseta, ptref, rho));
          suml2l3 += w * l2l3;
          const double l2l3res = 0.5 * (1.0 / getJec(c.at("jec"), +abseta, ptref, rho) +
                                        1.0 / getJec(c.at("jec"), -abseta, ptref, rho));
          suml2l3res += w * l2l3res;
        }
      }
      const double values[] = {sumw5 / sumw8, suml2l3 / sumw5, suml2l3res / sumw5,
                               suml2l3 / sumw5 * (suml2l3res / sumw5)};
      for (int ibin = 1; ibin != 5; ++ibin) {
        hruncl->SetBinContent(ibin, values[ibin - 1]);
        hruncl->GetXaxis()->SetBinLabel(ibin, cfg_.runclLabels[ibin - 1].c_str());
      }
    }

    TH1D* herr = hists.at("herr");
    for (int ipt = 1; ipt != herr->GetNbinsX() + 1; ++ipt) {
      fillBin(hists, ipt, herr->GetBinCenter(ipt), eta1, eta2, h2, c, getJec);
    }

    dout2->cd();
    for (const auto& spec : cfg_.histograms) {
      TH1D* h = hists.at(spec.key);
      if (spec.fill) {
        h->SetMarkerSize(0);
        h->SetFillStyle(1001);
        h->SetFillColorAlpha(inputTools::rootConstant(spec.fill->first, "Reprocess.json"), spec.fill->second);
      }
      h->Write();
    }
    hruncl->Write();
  }
  if (cfg_.verbose) std::printf("JEC inversion: %s\n", getJec.statistics().c_str());
}

void JecBands::fillBin(std::map<std::string, TH1D*>& hists, int ipt, double pt, double eta1, double eta2, TH2& h2,
                       const Correctors& c, JecInverter& getJec) const {
  TAxis* xaxis = h2.GetXaxis();
  const int jpt = std::max(1, std::min(xaxis->FindBin(pt), xaxis->GetNbins()));
  TH1D* heta = h2.ProjectionY(("heta_" + std::to_string(ipt)).c_str(), jpt, jpt);  // belongs to the output
  const int ieta2 = heta->FindBin(eta2), ieta1 = heta->FindBin(eta1);
  const int intw = static_cast<int>(heta->Integral(ieta1, ieta2 - 1));
  const int neta = 2 * (ieta2 - ieta1);
  if (!(eta2 > eta1 && eta1 >= 0)) throw JecSysError("reprocess: bad eta bin");
  const bool corLevelRes = corLevel_ == "L1L2L3Res";
  const double rhoAvg = cfg_.rhoAverage, rhoDt = cfg_.rhoData, rhoMc = cfg_.rhoMc;

  // Weighted sums; L1 terms are w / correction.
  double sVal = 0, sL2l3res = 0, sW = 0, sRun1 = 0, sJes = 0;
  std::map<std::string, double> s;
  std::map<std::string, double> errSum;
  for (int jeta = 0; jeta != neta; ++jeta) {
    const int keta = ieta1 + (jeta % (neta / 2));
    const double eta = (jeta < neta / 2 ? -1 : 1) * heta->GetBinCenter(keta);
    const double w = intw ? heta->GetBinContent(keta) / (2 * intw) : 1.0 / neta;
    if (!(w <= 1)) throw JecSysError("reprocess: eta weight above 1 at pT " + textTools::formatDouble(pt));

    // luminosity-weighted JES of the epoch (the per-epoch JEC of the macro is overwritten)
    double val = 0;
    for (std::size_t k = 0; k != c.combined.size(); ++k) {
      const double term = c.combined[k].first * (1.0 / getJec(*c.combined[k].second, eta, pt, rhoAvg));
      val = k == 0 ? term : val + term;
    }
    double jesrun1 = 1.0 / getJec(c.at("run1"), eta, pt, rhoAvg);
    double jes = 1.0 / getJec(c.at("old"), eta, pt, rhoAvg);
    const double vall2l3res = val;
    if (corLevelRes) {  // closure test: bands relative to the old JES
      val = val / jes;
      jesrun1 = jesrun1 / jes;
      jes = jes / jes;
    }
    sL2l3res += w * vall2l3res;
    sRun1 += w * jesrun1;
    sVal += w * val;
    sJes += w * jes;
    sW += w;

    const auto add = [&](const char* key, const char* corrector, double rho) {
      s[key] += w / getJec(c.at(corrector), eta, pt, rho);
    };
    add("l1flat", "l1flat", rhoAvg);
    add("l1rcdt", "l1rcdt", rhoAvg);
    add("l1pt", "l1pt", rhoAvg);
    add("l1mc", "l1mc", rhoAvg);
    add("l1rcmc", "l1rcmc", rhoAvg);
    add("l1rho", "l1pt", rhoAvg + 1);
    add("l1dt", "l1dt", rhoAvg);
    add("l1c", "l1c", rhoAvg);
    add("l1s", "l1s", rhoAvg);
    s["l1rc"] += w / std::max(getJec(c.at("l1rc"), eta, pt, rhoAvg), 0.0001);
    add("l2c", "l2c", rhoAvg);
    add("l2s", "l2s", rhoAvg);

    add("rcdnom", "rcdnom", rhoDt);
    add("rcmnom", "rcmnom", rhoMc);
    add("rcdalt", "rcdalt", rhoDt);
    add("rcmalt", "rcmalt", rhoMc);
    add("l1dnomsf", "l1dnomsf", rhoDt);
    add("l1daltsf", "l1daltsf", rhoDt);
    add("l1mnomsf", "l1mnomsf", rhoMc);
    add("l1dnom", "l1dnom", rhoDt);
    add("l1mnom", "l1mnom", rhoMc);
    add("l1dalt", "l1dalt", rhoDt);
    add("l1malt", "l1malt", rhoMc);
    add("l1dnomsf20", "l1dnomsf", rhoDt * 1.2);
    add("l1daltsf20", "l1daltsf", rhoDt * 1.2);
    add("l1dnom20", "l1dnom", rhoDt * 1.2);
    add("l1mnom20", "l1mnom", rhoMc * 1.2);
    add("l1dalt20", "l1dalt", rhoDt * 1.2);
    add("l1malt20", "l1malt", rhoMc * 1.2);

    const auto addErr = [&](const std::string& key, JetCorrectionUncertainty& source) {
      source.setJetEta(eta);
      source.setJetPt(pt);
      const double err = source.getUncertainty(true);
      errSum[key] += w * err * err;
    };
    addErr("total", *c.unc.at("total"));
    addErr("ref", *c.unc.at("ref"));
    addErr("ref1", *c.uncRef1);
    for (const auto& [key, source] : c.unc) {
      if (key != "total" && key != "ref") addErr(key, *source);
    }
  }

  const double sumw = sW;
  const double vall2l3res = sL2l3res / sumw, val = sVal / sumw;
  std::map<std::string, double> err;
  for (const auto& [key, value] : errSum) err[key] = std::sqrt(value / sumw);
  const auto mean = [&](const char* key) { return s[key] / sumw; };
  const auto h = [&](const char* key) -> TH1D& { return *hists.at(key); };

  for (const char* key : {"herr", "herr_ref", "herr_spr", "herr_noflv", "herr_mpf", "herr_pt"}) {
    h(key).SetBinContent(ipt, val);
  }
  h("herr_l2l3res").SetBinContent(ipt, vall2l3res);
  h("herr_pu").SetBinContent(ipt, 1);
  h("herr").SetBinError(ipt, val * err["total"]);
  h("herr_l2l3res").SetBinError(ipt, vall2l3res * err["ref"]);
  h("herr_ref").SetBinError(ipt, val * err["ref"]);
  h("herr_spr").SetBinError(ipt, val * std::sqrt(err["hcal"] * err["hcal"] + err["ecal"] * err["ecal"]));
  for (const char* key : {"glu", "uds", "cha", "bot", "zjt", "gjt", "qcd", "pu", "noflv"}) {
    h(("herr_" + std::string(key)).c_str()).SetBinError(ipt, val * err[key]);
  }
  h("herr_mpf").SetBinError(ipt, val * err["pt"]);
  h("herr_pt").SetBinError(ipt, val * std::sqrt(err["pt"] * err["pt"] + err["pu"] * err["pu"]));

  const double run1 = sRun1 / sumw;
  h("hrun1").SetBinContent(ipt, run1);
  h("hrun1").SetBinError(ipt, run1 * err["ref1"]);
  h("hjes").SetBinContent(ipt, sJes / sumw);

  const double l1pt = mean("l1pt"), l1mc = mean("l1mc"), l1rho = mean("l1rho"), l1flat = mean("l1flat");
  for (const auto& [key, value] : {std::pair<const char*, double>{"hl1bias", l1pt / l1flat},
                                   {"hl1diff", l1pt / l1mc},
                                   {"hl1drho", l1pt / l1rho}}) {
    h(key).SetBinContent(ipt, value);
    h(key).SetBinError(ipt, 0.5 * std::fabs(1 - value));
  }
  const double l1dt = mean("l1dt"), l1c = mean("l1c"), l1s = mean("l1s"), l1rc = mean("l1rc"),
               l1rcdt = mean("l1rcdt"), l1rcmc = mean("l1rcmc");
  h("hl1dtomc").SetBinContent(ipt, (1.0 / l1dt) / (1.0 / l1mc));
  h("hrcdtomc").SetBinContent(ipt, (1.0 / l1rcdt) / (1.0 / l1rcmc));
  h("hl1cos").SetBinContent(ipt, (1.0 / l1c) / (1.0 / l1s));
  h("hl1rcos").SetBinContent(ipt, (1.0 / l1rc) / (1.0 / l1s));
  const double l2c = mean("l2c"), l2s = mean("l2s");
  h("hl2cos").SetBinContent(ipt, (1.0 / l2c) / (1.0 / l2s));

  const double rcdnom = mean("rcdnom"), rcmnom = mean("rcmnom"), rcdalt = mean("rcdalt"), rcmalt = mean("rcmalt");
  h("hrcmpf").SetBinContent(ipt, (rcdalt / rcmalt) / (rcdnom / rcmnom));
  h("hrcmpfdt").SetBinContent(ipt, rcdalt / rcdnom);
  h("hrcmpfmc").SetBinContent(ipt, rcmalt / rcmnom);
  h("hrcdt").SetBinContent(ipt, rcdnom);
  h("hrcmc").SetBinContent(ipt, rcmnom);

  const double l1dnomsf = mean("l1dnomsf"), l1daltsf = mean("l1daltsf"), l1mnomsf = mean("l1mnomsf");
  h("hl1sf").SetBinContent(ipt, l1daltsf / l1dnomsf);
  h("hl1sfnom").SetBinContent(ipt, (l1dnomsf - 1) / (l1mnomsf - 1));
  h("hl1sfalt").SetBinContent(ipt, (l1daltsf - 1) / (l1mnomsf - 1));
  h("hl1offdt").SetBinContent(ipt, pt * (l1dnomsf - 1));
  h("hl1dt").SetBinContent(ipt, l1dnomsf);

  const double l1dnom = mean("l1dnom"), l1mnom = mean("l1mnom"), l1dalt = mean("l1dalt"), l1malt = mean("l1malt");
  h("hl1").SetBinContent(ipt, (l1dalt / l1malt) / (l1dnom / l1mnom));
  h("hl1d").SetBinContent(ipt, l1dalt / l1dnom);
  h("hl1m").SetBinContent(ipt, l1malt / l1mnom);
  h("hl1dnom").SetBinContent(ipt, l1dnom);
  h("hl1mnom").SetBinContent(ipt, l1mnom);

  const double l1dnomsf20 = mean("l1dnomsf20"), l1daltsf20 = mean("l1daltsf20");
  h("hl1sfr").SetBinContent(ipt, (l1daltsf20 / l1dnomsf20) / (l1daltsf / l1dnomsf));
  h("hl1sf20").SetBinContent(ipt, l1daltsf20 / l1dnomsf20);
  h("hl1dt20").SetBinContent(ipt, l1dnomsf20);

  const double l1dnom20 = mean("l1dnom20"), l1mnom20 = mean("l1mnom20"), l1dalt20 = mean("l1dalt20"),
               l1malt20 = mean("l1malt20");
  h("hl1r").SetBinContent(ipt, ((l1dalt20 / l1malt20) / (l1dnom20 / l1mnom20)) / ((l1dalt / l1malt) / (l1dnom / l1mnom)));
  h("hl120").SetBinContent(ipt, (l1dalt20 / l1malt20) / (l1dnom20 / l1mnom20));
  h("hl1d20").SetBinContent(ipt, l1dalt20 / l1dnom20);
}
