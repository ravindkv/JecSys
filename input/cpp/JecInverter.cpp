#include "JecInverter.h"

#include <cmath>
#include <cstdio>

#include "CondFormats/JetMETObjects/interface/FactorizedJetCorrector.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectionUncertainty.h"
#include "CondFormats/JetMETObjects/interface/JetCorrectorParameters.h"
#include "JecSysError.h"
#include "TF1.h"
#include "TextTools.h"

// ---------------------------------------------------------------- CorrectorCache

CorrectorCache::CorrectorCache(std::string directory, std::string algorithm, bool verbose)
    : directory_(std::move(directory)), algorithm_(std::move(algorithm)), verbose_(verbose) {}

CorrectorCache::~CorrectorCache() = default;

FactorizedJetCorrector& CorrectorCache::corrector(const std::vector<std::string>& tags) {
  auto& slot = correctors_[tags];
  if (!slot) {
    std::vector<JetCorrectorParameters> parameters;
    for (const std::string& tag : tags) {
      if (tag.empty()) continue;
      const std::string name = tag.find("_AK") != std::string::npos ? tag : tag + algorithm_;
      const std::string path = directory_ + "/" + name + ".txt";
      if (verbose_) std::printf("%s\n", path.c_str());
      if (!textTools::fileExists(path)) throw JecSysError("JEC file " + path + " not found");
      parameters.emplace_back(path);
    }
    slot = std::make_unique<FactorizedJetCorrector>(parameters);
  }
  return *slot;
}

JetCorrectionUncertainty& CorrectorCache::uncertainty(const std::string& file, const std::string& source) {
  const std::string path = directory_ + "/" + file;
  if (verbose_) std::printf("%s:%s\n", path.c_str(), source.c_str());
  if (!textTools::fileExists(path)) throw JecSysError("JEC uncertainty file " + path + " not found");
  parameters_.push_back(std::make_unique<JetCorrectorParameters>(path, source));
  uncertainties_.push_back(std::make_unique<JetCorrectionUncertainty>(*parameters_.back()));
  return *uncertainties_.back();
}

// ---------------------------------------------------------------- JecInverter

JecInverter::Mode JecInverter::parseMode(const std::string& name) {
  if (name == "getx") return Mode::GetX;
  if (name == "memo") return Mode::Memo;
  if (name == "secant") return Mode::Secant;
  throw JecSysError("JEC inversion must be getx, memo or secant, not '" + name + "'");
}

JecInverter::JecInverter(double jetArea, double xmin, double xmax, Mode mode)
    : jetArea_(jetArea), xmin_(xmin), xmax_(xmax), mode_(mode) {
  // funcCorrPt of reprocess.C: pT(reco) * JEC(pT(reco)); parameters eta and rho.
  function_ = std::make_unique<TF1>(
      "fCorrPt",
      [this](double* x, double* p) {
        return static_cast<double>(correction(*current_, p[0], x[0], p[1])) * x[0];
      },
      xmin, xmax, 2);
}

JecInverter::~JecInverter() = default;

double JecInverter::correction(FactorizedJetCorrector& jec, double eta, double pt, double rho) const {
  jec.setJetEta(eta);
  jec.setJetPt(pt);
  jec.setRho(rho);
  jec.setJetA(jetArea_);
  return jec.getCorrection();
}

double JecInverter::getX(FactorizedJetCorrector& jec, double eta, double ptcorr, double rho) {
  current_ = &jec;
  function_->SetParameters(eta, rho);
  const double ptreco = function_->GetX(ptcorr, xmin_, xmax_);
  return correction(jec, eta, ptreco, rho);
}

double JecInverter::secant(FactorizedJetCorrector& jec, double eta, double ptcorr, double rho) {
  const auto c = [&](double x) {
    ++evaluations_;
    return correction(jec, eta, x, rho);
  };
  // Two fixed-point steps from x = ptcorr, then secant steps. c is a float, so x*c(x) is
  // a staircase: stop on a plateau or at a 1e-9 step and keep the c of the smallest |g|.
  // A root on the edge between two neighbouring float values of c has no exact solution;
  // there the secant bounces and, after 30 steps, GetX decides (~2% of the solves). That
  // keeps the result closest to GetX (measured 2026-11-19: 28 of 3886 objects of 2016GH
  // differ, by <= 4.4e-8 absolute; returning the better side of the edge instead saves
  // 10% of the time but doubles the differences).
  double cx = c(ptcorr);
  double x0 = ptcorr / cx;
  cx = c(x0);
  double g0 = x0 * cx - ptcorr;
  double best = cx, bestG = std::fabs(g0);
  double x1 = ptcorr / cx;
  for (int iteration = 0; iteration != 30; ++iteration) {
    if (!(x1 >= xmin_ && x1 <= xmax_)) break;
    cx = c(x1);
    const double g1 = x1 * cx - ptcorr;
    if (std::fabs(g1) < bestG) {
      bestG = std::fabs(g1);
      best = cx;
    }
    if (g1 == 0 || g1 == g0 || std::fabs(x1 - x0) <= 1e-9 * x1) return best;
    const double x2 = x1 - g1 * (x1 - x0) / (g1 - g0);
    x0 = x1;
    g0 = g1;
    x1 = x2;
  }
  ++fallbacks_;
  return getX(jec, eta, ptcorr, rho);
}

double JecInverter::operator()(FactorizedJetCorrector& jec, double eta, double ptcorr, double rho) {
  ++calls_;
  if (mode_ == Mode::GetX) {
    ++solves_;
    return getX(jec, eta, ptcorr, rho);
  }
  // Repeated calls compute their arguments with the same expressions, so exact keys hit.
  const auto key = std::make_tuple(static_cast<const FactorizedJetCorrector*>(&jec), eta, ptcorr, rho);
  const auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  ++solves_;
  const double value = mode_ == Mode::Memo ? getX(jec, eta, ptcorr, rho) : secant(jec, eta, ptcorr, rho);
  cache_.emplace(key, value);
  return value;
}

std::string JecInverter::statistics() const {
  std::string text = std::to_string(calls_) + " inversions, " + std::to_string(solves_) + " solved";
  if (mode_ == Mode::Secant) {
    text += ", " + std::to_string(evaluations_) + " evaluations, " + std::to_string(fallbacks_) + " GetX fallbacks";
  }
  return text;
}
