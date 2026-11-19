#pragma once
// The JEC inversion of the reference bands (getJEC of reprocess.C): the correction at a
// corrected pT, i.e. c(x) with x*c(x) = ptcorr, where c is
// FactorizedJetCorrector::getCorrection() at (eta, x, rho, jet area). See input/README.md,
// "JEC inversion", for the measurements behind the modes.
//
// Modes:
//   getx    TF1::GetX on [xmin, xmax] for every call, as the macro (the reference);
//   memo    the same, but repeated calls (same corrector, eta, ptcorr, rho) are answered
//           from a cache: identical results, about half the solves;
//   secant  a float-aware secant from x = ptcorr/c(ptcorr), with the cache and a GetX
//           fallback: ~8x fewer evaluations, but the result can differ from GetX by a
//           float step of c (<= 2e-6 relative), so the output is not byte-identical.
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

class FactorizedJetCorrector;
class JetCorrectionUncertainty;
class JetCorrectorParameters;
class TF1;

// One FactorizedJetCorrector per list of tags (shared: every evaluation sets eta, pT, rho
// and area again), read from <directory>/<tag><algorithm>.txt.
class CorrectorCache {
 public:
  CorrectorCache(std::string directory, std::string algorithm, bool verbose);
  ~CorrectorCache();
  // [L1, L2, residual] tags; empty tags are skipped; "_AK..." in a tag means it already
  // names the algorithm.
  FactorizedJetCorrector& corrector(const std::vector<std::string>& tags);
  // One uncertainty source of an uncertainty file in the directory.
  JetCorrectionUncertainty& uncertainty(const std::string& file, const std::string& source);

 private:
  std::string directory_, algorithm_;
  bool verbose_;
  std::map<std::vector<std::string>, std::unique_ptr<FactorizedJetCorrector>> correctors_;
  std::vector<std::unique_ptr<JetCorrectorParameters>> parameters_;
  std::vector<std::unique_ptr<JetCorrectionUncertainty>> uncertainties_;
};

class JecInverter {
 public:
  enum class Mode { GetX, Memo, Secant };
  static Mode parseMode(const std::string& name);  // "getx", "memo", "secant"

  JecInverter(double jetArea, double xmin, double xmax, Mode mode);
  ~JecInverter();

  double operator()(FactorizedJetCorrector& jec, double eta, double ptcorr, double rho);

  // calls, solves (not answered from the cache), evaluations of c (secant), GetX fallbacks
  std::string statistics() const;

 private:
  double correction(FactorizedJetCorrector& jec, double eta, double pt, double rho) const;
  double getX(FactorizedJetCorrector& jec, double eta, double ptcorr, double rho);
  double secant(FactorizedJetCorrector& jec, double eta, double ptcorr, double rho);

  double jetArea_, xmin_, xmax_;
  Mode mode_;
  std::unique_ptr<TF1> function_;              // "fCorrPt": x*c(x), parameters eta and rho
  FactorizedJetCorrector* current_ = nullptr;  // the corrector the TF1 evaluates
  std::map<std::tuple<const FactorizedJetCorrector*, double, double, double>, double> cache_;
  long calls_ = 0, solves_ = 0, evaluations_ = 0, fallbacks_ = 0;
};
