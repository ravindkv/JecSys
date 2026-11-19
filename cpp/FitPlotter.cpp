#include "FitPlotter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "InputFile.h"
#include "JecSysError.h"
#include "TCanvas.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TLatex.h"
#include "TLegend.h"
#include "TLine.h"
#include "TROOT.h"
#include "TStyle.h"
#include "TextTools.h"

namespace {

void applyStyle() {
  gROOT->SetBatch(true);
  gStyle->SetOptStat(0);
  gStyle->SetOptTitle(0);
  gStyle->SetPadTickX(1);
  gStyle->SetPadTickY(1);
  gStyle->SetPadLeftMargin(0.13);
  gStyle->SetPadRightMargin(0.04);
  gStyle->SetPadTopMargin(0.07);
  gStyle->SetPadBottomMargin(0.13);
  gStyle->SetTitleSize(0.05, "XY");
  gStyle->SetLabelSize(0.045, "XY");
  gStyle->SetTitleOffset(1.2, "Y");
  gStyle->SetLegendBorderSize(0);
  gStyle->SetLegendFillColor(0);
  gStyle->SetEndErrorSize(0);
}

struct ChannelStyle {
  int color;
  int marker;
};

// Colour by sample, full marker for MPF and open marker for pT balance.
ChannelStyle styleOf(const std::string& channel, int index) {
  static const std::vector<std::pair<std::string, int>> sampleColors = {
      {"multijet", kBlack}, {"gamjet", kBlue + 1}, {"zjet", kRed + 1},   {"zlljet", kRed + 1},
      {"zeejet", kGreen + 2}, {"zmmjet", kRed + 1}, {"hadw", kGreen + 2}, {"incjet", kOrange + 2},
      {"pfjet", kBlack}};
  static const int fullMarkers[] = {kFullCircle, kFullSquare, kFullTriangleUp, kFullDiamond, kFullTriangleDown};
  static const int openMarkers[] = {kOpenCircle, kOpenSquare, kOpenTriangleUp, kOpenDiamond, kOpenTriangleDown};
  ChannelStyle style{kGray + 2, kFullCircle};
  int sampleIndex = index;
  for (std::size_t i = 0; i != sampleColors.size(); ++i) {
    if (channel.find(sampleColors[i].first) != std::string::npos) {
      style.color = sampleColors[i].second;
      sampleIndex = static_cast<int>(i);
    }
  }
  const bool open = channel.find("ptchs") != std::string::npos;
  style.marker = (open ? openMarkers : fullMarkers)[sampleIndex % 5];
  return style;
}

void header(const nlohmann::json& result, const std::string& what) {
  TLatex text;
  text.SetNDC();
  text.SetTextSize(0.042);
  text.DrawLatex(0.13, 0.945, "#bf{CMS} #it{internal}");
  text.SetTextAlign(31);
  text.DrawLatex(0.96, 0.945,
                 (result.value("epoch", "") + ", " + result.value("corLevel", "") + ", " + what).c_str());
}

void save(TCanvas& canvas, const std::string& path) {
  // Page with the proportions of the canvas, so the plot fills the PDF page.
  gStyle->SetPaperSize(20., 20. * canvas.GetWh() / canvas.GetWw());
  canvas.SaveAs(path.c_str());
  std::cout << "Wrote " << path << "\n";
}

}  // namespace

void FitPlotter::run(const std::string& fitFile, const std::string& outDir) {
  applyStyle();
  const InputFile in(fitFile);
  nlohmann::json result;
  try {
    result = nlohmann::json::parse(in.getText("_meta/fit/result"));
  } catch (const nlohmann::json::exception& error) {
    throw JecSysError(fitFile + ": cannot read the fit summary (" + error.what() + ")");
  }
  const std::string dir = outDir.empty() ? textTools::dirName(fitFile) : outDir;
  textTools::makeDirs(dir);
  const std::string stem = dir + "/" + std::filesystem::path(fitFile).stem().string();
  const std::string legacy = "ratio/" + result.value("etaBin", "eta00-13") + "/sys/";

  std::vector<std::string> responseChannels, pfChannels;
  for (const auto& channel : result["channels"]) {
    if (channel.value("masked", false)) continue;
    (channel["kind"] == "pfFraction" ? pfChannels : responseChannels).push_back(channel["name"]);
  }
  char chi2Text[128];
  std::snprintf(chi2Text, sizeof(chi2Text), "#chi^{2} / ndf = %.1f / %d", result.value("chi2", 0.),
                result.value("ndf", 0));

  // Everything drawn must outlive the canvas that shows it.
  std::vector<std::unique_ptr<TObject>> keep;
  const auto own = [&keep](auto object) {
    auto* raw = object.release();
    keep.emplace_back(raw);
    return raw;
  };

  // ---- response: pre-fit and post-fit ----
  const auto hjesfit = in.find<TH1D>(legacy + "hjesfit");
  for (const std::string stage : {"prefit", "postfit"}) {
    if (responseChannels.empty()) break;
    TCanvas canvas(("c_" + stage).c_str(), "", 700, 600);
    canvas.SetLogx();
    TH1D frame("frame", ";p_{T} (GeV);Jet response (data / MC)", 100, 15., 4500.);
    frame.SetDirectory(nullptr);
    frame.SetMinimum(0.94);
    frame.SetMaximum(1.07);
    frame.GetXaxis()->SetMoreLogLabels();
    frame.GetXaxis()->SetNoExponent();
    frame.Draw("AXIS");

    TH1D* fitLine = nullptr;
    if (hjesfit) {
      auto* band = own(std::unique_ptr<TH1D>(static_cast<TH1D*>(hjesfit->Clone("band"))));
      band->SetFillColorAlpha(kYellow + 1, 0.6);
      band->SetLineColor(kBlack);
      band->SetMarkerSize(0);
      band->Draw("E3 SAME");
      auto* line = own(std::unique_ptr<TH1D>(static_cast<TH1D*>(hjesfit->Clone("line"))));
      line->SetLineColor(kBlack);
      line->SetLineWidth(2);
      line->SetFillStyle(0);
      line->Draw("HIST L SAME");
      fitLine = band;  // legend entry: line with the band colour
    }
    TLine unity;
    unity.SetLineStyle(kDashed);
    unity.DrawLine(15., 1., 4500., 1.);

    TLegend legend(0.60, 0.90 - 0.04 * (responseChannels.size() + 1), 0.94, 0.90);
    legend.SetTextSize(0.035);
    for (std::size_t i = 0; i != responseChannels.size(); ++i) {
      auto* graph = own(in.get<TGraphErrors>(stage + "/" + responseChannels[i]));
      const ChannelStyle style = styleOf(responseChannels[i], static_cast<int>(i));
      graph->SetMarkerStyle(style.marker);
      graph->SetMarkerColor(style.color);
      graph->SetLineColor(style.color);
      graph->SetMarkerSize(0.9);
      graph->Draw("PZ SAME");
      legend.AddEntry(graph, responseChannels[i].c_str(), "PL");
    }
    if (fitLine) legend.AddEntry(fitLine, "fit #pm unc.", "LF");
    legend.Draw();
    TLatex text;
    text.SetNDC();
    text.SetTextSize(0.04);
    text.DrawLatex(0.17, 0.19, chi2Text);
    text.DrawLatex(0.17, 0.85,
                   stage == "prefit" ? "Before the fit (FSR corrected)" : "After the fit (nuisances applied)");
    if (stage == "prefit") {
      text.SetTextSize(0.03);
      text.DrawLatex(0.17, 0.80, "multijet: ratio to the recoil, not a response");
    }
    header(result, result.value("etaBin", ""));
    canvas.RedrawAxis();
    save(canvas, stem + "_" + stage + ".pdf");
  }

  // ---- PF fractions ----
  if (!pfChannels.empty()) {
    TCanvas canvas("c_pf", "", 700, 600);
    canvas.SetLogx();
    TH1D frame("framePf", ";p_{T} (GeV);PF energy fraction, data #minus MC (10^{-2})", 100, 15., 4500.);
    frame.SetDirectory(nullptr);
    frame.SetMinimum(-2.5);
    frame.SetMaximum(3.5);
    frame.GetXaxis()->SetMoreLogLabels();
    frame.GetXaxis()->SetNoExponent();
    frame.Draw("AXIS");
    TLegend legend(0.60, 0.90 - 0.04 * pfChannels.size(), 0.94, 0.90);
    legend.SetTextSize(0.035);
    static const std::vector<std::pair<std::string, int>> fractionColors = {
        {"chf", kRed + 1}, {"nhf", kGreen + 2}, {"nef", kBlue + 1}};
    for (std::size_t i = 0; i != pfChannels.size(); ++i) {
      int color = kBlack;
      for (const auto& [fraction, fractionColor] : fractionColors) {
        if (pfChannels[i].rfind(fraction, 0) == 0) color = fractionColor;
      }
      const bool dijet = pfChannels[i].find("pfjet") != std::string::npos;
      const auto scale = [](TGraphErrors& graph) {  // to units of 10^-2
        for (int k = 0; k != graph.GetN(); ++k) {
          graph.SetPoint(k, graph.GetX()[k], 100 * graph.GetY()[k]);
          graph.SetPointError(k, graph.GetEX()[k], 100 * graph.GetEY()[k]);
        }
      };
      auto* data = own(in.get<TGraphErrors>("postfit/" + pfChannels[i]));
      scale(*data);
      data->SetMarkerStyle(dijet ? kFullCircle : kOpenSquare);
      data->SetMarkerSize(0.8);
      data->SetMarkerColor(color);
      data->SetLineColor(color);
      data->Draw("PZ SAME");
      auto* model = own(in.get<TGraphErrors>("model/" + pfChannels[i]));
      scale(*model);
      model->SetLineColor(color);
      model->SetLineWidth(2);
      model->SetLineStyle(dijet ? kSolid : kDashed);
      model->Draw("LX SAME");
      legend.AddEntry(data, pfChannels[i].c_str(), "PL");
    }
    legend.Draw();
    TLatex text;
    text.SetNDC();
    text.SetTextSize(0.04);
    text.DrawLatex(0.17, 0.85, "Points: data, lines: model");
    header(result, "PF composition");
    canvas.RedrawAxis();
    save(canvas, stem + "_pf.pdf");
  }

  // ---- nuisance pulls ----
  const auto& nuisances = result["nuisances"];
  if (!nuisances.empty()) {
    const int n = static_cast<int>(nuisances.size());
    TCanvas canvas("c_pulls", "", 900, 600);
    canvas.SetBottomMargin(0.42);
    canvas.SetGridy();
    TH1D pulls("pulls", ";;Post-fit nuisance value", n, 0, n);
    pulls.SetDirectory(nullptr);
    for (int i = 0; i != n; ++i) {
      pulls.SetBinContent(i + 1, nuisances[i].value("value", 0.));
      pulls.SetBinError(i + 1, nuisances[i].value("error", 0.));
      pulls.GetXaxis()->SetBinLabel(i + 1, nuisances[i].value("name", "").c_str());
    }
    pulls.GetXaxis()->LabelsOption("v");
    pulls.GetXaxis()->SetLabelSize(0.03);
    pulls.SetMinimum(-2.5);
    pulls.SetMaximum(2.5);
    pulls.SetMarkerStyle(kFullCircle);
    pulls.SetLineColor(kBlack);
    pulls.Draw("E1");
    TLatex text;
    text.SetNDC();
    text.SetTextSize(0.035);
    text.DrawLatex(0.60, 0.87, "Before the fit: 0 #pm 1");
    header(result, chi2Text);
    save(canvas, stem + "_pulls.pdf");
  }
}
