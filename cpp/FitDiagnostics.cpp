#include "FitDiagnostics.h"

#include <cmath>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "JecSysError.h"
#include "OutputFile.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TMath.h"
#include "TMatrixD.h"
#include "TVectorD.h"
#include "TextTools.h"

namespace {

// Histogram with the output binning, emptied.
std::unique_ptr<TH1D> emptyClone(const TH1D& binning, const std::string& name) {
  std::unique_ptr<TH1D> hist(static_cast<TH1D*>(binning.Clone(name.c_str())));
  hist->SetDirectory(nullptr);
  hist->Reset();
  return hist;
}

TVectorD toVector(const std::vector<double>& values) {
  TVectorD vector(static_cast<int>(values.size()));
  for (std::size_t i = 0; i != values.size(); ++i) vector[i] = values[i];
  return vector;
}

}  // namespace

void FitDiagnostics::run(FitContext& context) const {
  const Workspace& ws = context.workspace;
  const Chi2Function& chi2 = context.chi2;
  const int nModel = chi2.nModelPar();
  const int nPar = chi2.nPar();

  const FitResult fit = FitMinimizer::minimize(chi2, context.parameters, context.options.verbose ? 0 : -1);
  const double* par = fit.values.data();
  const double* theta = par + nModel;
  const Chi2Breakdown breakdown = chi2.breakdown(par);
  fitTools::printResult(context, fit, breakdown);

  OutputFile out(context.outputPath("fit", "root"));
  const std::string legacy = "ratio/" + ws.card().etaBin + "/";

  // ---- parameters and covariance (was sys/emat, vpar, verr, h2emat, h2cov, h1par) ----
  const TMatrixD& emat = fit.covariance;
  TH2D h2emat("h2emat", ";i_{par};j_{par}", nPar, -0.5, nPar - 0.5, nPar, -0.5, nPar - 0.5);
  TH2D h2cov("h2cov", ";i_{par};j_{par}", nPar, -0.5, nPar - 0.5, nPar, -0.5, nPar - 0.5);
  TH1D h1par("h1par", ";i_{par};", nPar, -0.5, nPar - 0.5);
  for (TH1* h : {static_cast<TH1*>(&h2emat), static_cast<TH1*>(&h2cov), static_cast<TH1*>(&h1par)}) {
    h->SetDirectory(nullptr);
  }
  for (int i = 0; i != nPar; ++i) {
    h1par.SetBinContent(i + 1, fit.values[i]);
    h1par.SetBinError(i + 1, fit.errors[i]);
    for (int j = 0; j != nPar; ++j) {
      h2emat.SetBinContent(i + 1, j + 1, emat[i][j]);
      h2cov.SetBinContent(i + 1, j + 1,
                          emat[i][i] * emat[j][j] == 0 ? 0 : emat[i][j] / std::sqrt(emat[i][i] * emat[j][j]));
    }
    const char* label = fit.parameters[i].name.c_str();
    h2emat.GetYaxis()->SetBinLabel(i + 1, label);
    h2cov.GetYaxis()->SetBinLabel(i + 1, label);
    h1par.GetXaxis()->SetBinLabel(i + 1, label);
  }
  out.write(legacy + "sys/emat", emat);
  out.write(legacy + "sys/vpar", toVector(fit.values));
  out.write(legacy + "sys/verr", toVector(fit.errors));
  out.write(legacy + "sys/h2emat", h2emat);
  out.write(legacy + "sys/h2cov", h2cov);
  out.write(legacy + "sys/h1par", h1par);

  // ---- JES fit with its eigenvectors (was sys/hjesfit*, eigvec, eigmat) ----
  if (const TH1D* binning = ws.ptBinning()) {
    TMatrixD modelEmat(nModel, nModel);
    for (int i = 0; i != nModel; ++i) {
      for (int j = 0; j != nModel; ++j) modelEmat[i][j] = emat[i][j];
    }
    TVectorD eigvec(nModel);
    const TMatrixD eigmat = modelEmat.EigenVectors(eigvec);

    const int nBins = binning->GetNbinsX();
    std::vector<std::unique_ptr<TH1D>> eigenHists;
    for (int ieig = 0; ieig != nModel; ++ieig) {
      // Model moved along one eigenvector of the covariance, and one parameter alone.
      std::vector<double> shifted(nModel), single(nModel, 0.), singleUp(nModel, 0.);
      for (int i = 0; i != nModel; ++i) shifted[i] = par[i] + eigmat[i][ieig] * std::sqrt(eigvec[ieig]);
      single[ieig] = par[ieig];
      singleUp[ieig] = par[ieig] + std::sqrt(emat[ieig][ieig]);

      auto heig = emptyClone(*binning, "hkeig_" + std::to_string(ieig));
      auto hcomp = emptyClone(*binning, "hcomp_" + std::to_string(ieig));
      for (int bin = 1; bin != nBins + 1; ++bin) {
        const double pt = binning->GetBinCenter(bin);
        heig->SetBinContent(bin, context.jes(pt, shifted.data()) - context.jes(pt, par));
        const double component = context.jes(pt, single.data());
        hcomp->SetBinContent(bin, component);
        hcomp->SetBinError(bin, std::fabs(context.jes(pt, singleUp.data()) - component));
      }
      out.write(legacy + "sys/hjesfit_eig" + std::to_string(ieig), *heig);
      out.write(legacy + "sys/hjesfit_par" + std::to_string(ieig), *hcomp);
      eigenHists.push_back(std::move(heig));
    }

    auto hjesfit = emptyClone(*binning, "hjesfit");
    auto hjesfit2 = emptyClone(*binning, "hjesfit2");  // before division by the closure reference
    for (int bin = 1; bin != nBins + 1; ++bin) {
      double sumE2 = 0;
      for (const auto& heig : eigenHists) sumE2 += std::pow(heig->GetBinContent(bin), 2);
      const double pt = binning->GetBinCenter(bin);
      const double jes = context.jes(pt, par);
      hjesfit->SetBinContent(bin, jes);
      hjesfit->SetBinError(bin, std::sqrt(sumE2));
      hjesfit2->SetBinContent(bin, jes * context.jesRef(pt));
      hjesfit2->SetBinError(bin, std::sqrt(sumE2));
    }
    out.write(legacy + "sys/hjesfit", *hjesfit);
    out.write(legacy + "sys/hjesfit2", *hjesfit2);
    out.write(legacy + "sys/eigvec", eigvec);
    out.write(legacy + "sys/eigmat", eigmat);
  } else {
    std::cerr << "WARNING: the workspace has no ptBinning; hjesfit and its eigenvectors are not written\n";
  }

  // ---- refitted FSR corrections (was fsr/hkfsr2_*) ----
  const CardGroup* fsrGroup = nullptr;
  for (const CardGroup& group : ws.card().groups) {
    if (group.name == "fsr") fsrGroup = &group;
  }
  for (std::size_t c = 0; fsrGroup && c != ws.channels().size(); ++c) {
    const WorkspaceChannel& channel = ws.channels()[c];
    if (!channel.fsr) continue;
    // FSR nuisances of this channel, in group order.
    std::vector<int> members;
    for (const std::string& name : fsrGroup->nuisances) {
      const int n = ws.nuisanceIndex(name);
      if (ws.nuisances()[n].shapes[c]) members.push_back(n);
    }
    auto refitted = emptyClone(*channel.fsr, "hkfsr2_" + channel.name);
    for (int bin = 1; bin != refitted->GetNbinsX() + 1; ++bin) {
      double shift = 0;
      std::vector<double> size(members.size());
      for (std::size_t k = 0; k != members.size(); ++k) {
        size[k] = ws.nuisances()[members[k]].shapes[c]->GetBinContent(bin);
        shift += size[k] * theta[members[k]];
      }
      double err2 = 0;  // error propagation over the nuisances of this channel
      for (std::size_t k = 0; k != members.size(); ++k) {
        for (std::size_t l = 0; l != members.size(); ++l) {
          err2 += emat[nModel + members[k]][nModel + members[l]] * size[k] * size[l];
        }
      }
      refitted->SetBinContent(bin, channel.fsr->GetBinContent(bin) - shift);
      refitted->SetBinError(bin, std::sqrt(err2));
    }
    out.write(legacy + "fsr/hkfsr2_" + channel.name, *refitted);
    for (std::size_t k = 0; k != members.size(); ++k) {
      const std::string name = "hkfsr2_" + channel.name + "_eig" + std::to_string(k);
      std::unique_ptr<TH1D> scaled(static_cast<TH1D*>(ws.nuisances()[members[k]].shapes[c]->Clone(name.c_str())));
      scaled->SetDirectory(nullptr);
      scaled->Scale(theta[members[k]]);
      out.write(legacy + "fsr/" + name, *scaled);
    }
  }

  // ---- pre-fit and post-fit graphs per channel, for plotting ----
  nlohmann::json channelsJson = nlohmann::json::array();
  for (std::size_t c = 0; c != chi2.channels().size(); ++c) {
    const Channel& channel = *chi2.channels()[c];
    const std::vector<PointResult> points = channel.evaluate(par, theta);
    const int n = static_cast<int>(points.size());
    TGraphErrors postfit(n), model(n), recoil(n);
    bool hasRecoil = false;
    for (int i = 0; i != n; ++i) {
      const PointResult& p = points[i];
      postfit.SetPoint(i, p.pt, p.value);
      postfit.SetPointError(i, p.ptError, p.sigma);
      model.SetPoint(i, p.pt, p.prediction);
      if (p.hasRecoil) {
        hasRecoil = true;
        recoil.SetPoint(i, p.recoilPt, p.recoilValue);
        recoil.SetPointError(i, p.ptError, p.sigma);
      }
    }
    postfit.SetName((channel.name() + "_postfit").c_str());
    model.SetName((channel.name() + "_model").c_str());
    recoil.SetName((channel.name() + "_recoil").c_str());
    out.write("prefit/" + channel.name(), *ws.channels()[c].data);
    out.write("postfit/" + channel.name(), postfit);
    out.write("model/" + channel.name(), model);
    if (hasRecoil) out.write("postfit/" + channel.name() + "_recoil", recoil);

    channelsJson.push_back({{"name", channel.name()},
                            {"kind", toString(channel.kind())},
                            {"masked", chi2.isMasked(c)},
                            {"chi2", breakdown.channelChi2[c]},
                            {"points", breakdown.channelPoints[c]}});
  }

  // ---- result.json ----
  const int nDof = fitTools::ndf(context, fit, breakdown);
  nlohmann::json result;
  result["epoch"] = ws.card().epoch;
  result["corLevel"] = ws.card().corLevel;
  result["etaBin"] = ws.card().etaBin;
  result["model"] = context.model.name();
  result["workspace"] = ws.path();
  result["gitHash"] = fitTools::gitHash();
  result["minuitStatus"] = fit.status;
  result["chi2"] = breakdown.total;
  result["nPoints"] = breakdown.nPoints;
  result["ndf"] = nDof;
  result["probability"] = TMath::Prob(breakdown.total, nDof);
  result["chi2Nuisances"] = breakdown.nuisanceChi2;
  result["chi2Penalty"] = breakdown.penaltyChi2;
  result["channels"] = channelsJson;
  result["parameters"] = nlohmann::json::array();
  result["nuisances"] = nlohmann::json::array();
  for (int i = 0; i != nPar; ++i) {
    result[i < nModel ? "parameters" : "nuisances"].push_back({{"name", fit.parameters[i].name},
                                                              {"value", fit.values[i]},
                                                              {"error", fit.errors[i]},
                                                              {"frozen", fit.parameters[i].fixed}});
  }
  out.writeText("_meta/fit/result", result.dump(2));
  out.writeText("_meta/fit/card", ws.card().toText());
  out.commit();

  const std::string jsonPath = context.outputPath("result", "json");
  std::ofstream json(jsonPath);
  if (!json) throw JecSysError("Cannot write " + jsonPath);
  json << result.dump(2) << "\n";
  std::cout << "Wrote " << out.path() << " and " << jsonPath << "\n";
}
