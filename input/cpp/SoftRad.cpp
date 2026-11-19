#include "SoftRad.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <tuple>

#include "JecSysError.h"
#include "TF1.h"
#include "TFile.h"
#include "TGraphErrors.h"
#include "TH1.h"
#include "TROOT.h"
#include "TextTools.h"

namespace {

using textTools::formatTemplate;

const char* kClass = "SoftRad";

// The points of an input graph.
struct Points {
  std::vector<double> x, y, ey;
};

Points readPoints(TDirectory& dir, const std::string& name, const std::string& where) {
  std::unique_ptr<TGraphErrors> graph(inputTools::get<TGraphErrors>(dir, name, where));  // graphs belong to the caller
  Points points;
  for (int i = 0; i != graph->GetN(); ++i) {
    points.x.push_back(graph->GetX()[i]);
    points.y.push_back(graph->GetY()[i]);
    points.ey.push_back(graph->GetEY()[i]);
  }
  return points;
}

// softrad3.C getY(): y and ey of the FIRST point with ptmin < x < ptmax (0, 0 if none).
// The macro meant the point closest to the bin centre, but never updates dx (toDo.txt A1).
std::pair<double, double> firstInBin(double ptmin, double ptmax, const Points& points) {
  for (std::size_t i = 0; i != points.x.size(); ++i) {
    if (ptmin < points.x[i] && points.x[i] < ptmax) return {points.y[i], points.ey[i]};
  }
  return {0.0, 0.0};
}

SoftRad::Function parseFunction(const SettingsReader& functions, const std::string& role) {
  const SettingsReader f = functions.child(role, {"name", "formula", "range"});
  const auto [xmin, xmax] = f.range("range");
  return {f.get<std::string>("name"), f.get<std::string>("formula"), xmin, xmax};
}

SoftRad::ModelParameters parseModel(const SettingsReader& model, const std::string& key) {
  const SettingsReader m = model.child(key, {"Rn", "Ru", "R2"});
  return {m.get<double>("Rn"), m.get<double>("Ru"), m.get<double>("R2")};
}

void requireAll(const std::vector<std::string>& list, const std::set<std::string>& needed, const std::string& what,
                const std::string& where) {
  for (const std::string& item : needed) {
    if (std::find(list.begin(), list.end(), item) == list.end()) {
      throw JecSysError(where + ": '" + what + "' must contain " + item);
    }
  }
}

}  // namespace

SoftRad::Settings SoftRad::Settings::parse(const nlohmann::json& json, const std::string& where) {
  const SettingsReader r(json, where,
                         {"readDirs", "readMethods", "writeDirs", "methods", "flavors", "samples", "alphaRef",
                          "countsFrom", "model", "functions", "checks", "names", "hdmClone", "fsrComponents"});
  Settings s;
  s.readDirs = r.get<std::vector<std::string>>("readDirs");
  s.readMethods = r.get<std::vector<std::string>>("readMethods");
  s.writeDirs = r.get<std::vector<std::string>>("writeDirs");
  s.methods = r.get<std::vector<std::string>>("methods");
  requireAll(s.readDirs, {"data", "mc", "ratio"}, "readDirs", where);
  requireAll(s.readMethods, {"counts", "mpf1", "mpfn", "mpfu", "mpfchs1", "ptchs"}, "readMethods", where);
  for (const std::string& method : s.methods) {
    if (method != "mpfchs1" && method != "ptchs") throw JecSysError(where + ": methods are mpfchs1 and ptchs");
  }
  for (const std::string& dir : s.writeDirs) {
    if (dir != "ratio" && dir != "data" && dir != "mc") throw JecSysError(where + ": writeDirs are ratio, data, mc");
  }
  s.flavors = r.get<std::string>("flavors");
  s.samples = r.get<std::map<std::string, std::vector<std::string>>>("samples");
  s.alphaRef = r.get<std::map<std::string, int>>("alphaRef");
  if (!s.alphaRef.count("default")) throw JecSysError(where + ": alphaRef needs a 'default'");
  s.alphaRefDefault = s.alphaRef.at("default");
  s.countsFrom = r.get<std::map<std::string, std::string>>("countsFrom");

  const SettingsReader model = r.child("model", {"default", "multijet", "variations", "k", "rootRange"});
  s.modelDefault = parseModel(model, "default");
  s.modelMultijet = parseModel(model, "multijet");
  const SettingsReader variations = model.child("variations", {"dRnd", "dRnm", "dRud", "dRum"});
  s.dRnd = variations.get<double>("dRnd");
  s.dRnm = variations.get<double>("dRnm");
  s.dRud = variations.get<double>("dRud");
  s.dRum = variations.get<double>("dRum");
  s.k = model.get<double>("k");
  std::tie(s.rootMin, s.rootMax) = model.range("rootRange");

  const SettingsReader functions = r.child("functions", {"mpf", "ptbal", "mpfMultijet", "ptbalMultijet"});
  s.mpf = parseFunction(functions, "mpf");
  s.ptbal = parseFunction(functions, "ptbal");
  s.mpfMultijet = parseFunction(functions, "mpfMultijet");
  s.ptbalMultijet = parseFunction(functions, "ptbalMultijet");

  const SettingsReader checks =
      r.child("checks", {"multijetMpf", "zjetMpf", "zjetMpfWarning", "ptbalWarning", "sumRuleWarning"});
  s.multijetMpf = checks.get<double>("multijetMpf");
  s.zjetMpf = checks.get<double>("zjetMpf");
  s.zjetMpfWarning = checks.get<double>("zjetMpfWarning");
  s.ptbalWarning = checks.get<double>("ptbalWarning");
  s.sumRuleWarning = checks.get<double>("sumRuleWarning");

  const SettingsReader names = r.child("names", {"etaDir", "input", "fsrDir", "fsr", "hdmDir", "hdm"});
  s.etaDirName = names.get<std::string>("etaDir");
  s.inputName = names.get<std::string>("input");
  s.fsrDirName = names.get<std::string>("fsrDir");
  s.fsrName = names.get<std::string>("fsr");
  s.hdmDirName = names.get<std::string>("hdmDir");
  s.hdmName = names.get<std::string>("hdm");
  s.hdmClone = r.get<std::string>("hdmClone");
  s.fsrComponents = r.get<std::vector<std::pair<std::string, std::string>>>("fsrComponents");
  if (s.fsrComponents.size() != 9) {
    throw JecSysError(where + ": fsrComponents must list the FSR histogram and its 8 components");
  }
  return s;
}

SoftRad::SoftRad(const Epoch& epoch, const nlohmann::json& stepArguments, bool verbose)
    : epoch_(epoch),
      verbose_(verbose),
      settings_(Settings::parse(inputTools::loadSettings(kClass, epoch), inputTools::settingsWhere(kClass, epoch))),
      style_(inputTools::loadSettings("TdrStyle", epoch), inputTools::settingsWhere("TdrStyle", epoch)) {
  const SettingsReader arguments(stepArguments, inputTools::configPath("InputChain") + ": softRad step",
                                 {"etamin", "etamax"});
  etamin_ = arguments.get<double>("etamin");
  etamax_ = arguments.get<double>("etamax");
  const std::string eta = formatTemplate(settings_.etaDirName, {{"eta1", static_cast<int>(10 * etamin_)},
                                                                {"eta2", static_cast<int>(10 * etamax_)}});
  if (!settings_.samples.count(eta)) {
    throw JecSysError(inputTools::settingsWhere(kClass, epoch) + ": no samples for " + eta);
  }
}

std::string SoftRad::name() const { return "softRad_" + std::to_string(static_cast<int>(10 * etamax_)); }

std::vector<std::string> SoftRad::expandSamples(const std::vector<std::string>& patterns, const std::string& flavors,
                                                std::set<std::string>& mcOnly) {
  std::vector<std::string> samples;
  for (const std::string& pattern : patterns) {
    const auto ends = [&](const char* suffix) {
      const std::string s(suffix);
      return pattern.size() >= s.size() && pattern.compare(pattern.size() - s.size(), s.size(), s) == 0;
    };
    if (ends("**")) {
      const std::string base = pattern.substr(0, pattern.size() - 2);
      for (char tag : flavors) {
        for (char gen : flavors) {
          samples.push_back(base + tag + gen);
          mcOnly.insert(samples.back());
        }
      }
    } else if (ends("*")) {
      const std::string base = pattern.substr(0, pattern.size() - 1);
      for (char tag : flavors) samples.push_back(base + tag);
    } else {
      samples.push_back(pattern);
    }
  }
  return samples;
}

void SoftRad::run(const std::string& path) {
  const Settings& cfg = settings_;
  const std::string eta = formatTemplate(cfg.etaDirName, {{"eta1", static_cast<int>(10 * etamin_)},
                                                          {"eta2", static_cast<int>(10 * etamax_)}});
  std::set<std::string> mcOnly;
  const std::vector<std::string> samples = expandSamples(cfg.samples.at(eta), cfg.flavors, mcOnly);
  if (verbose_) std::printf("Calling softrad3(%g,%g) for epoch %s\n", etamin_, etamax_, epoch_.name.c_str());

  inputTools::requireFile(path, name());
  style_.setAsInChain();
  gROOT->cd();
  std::unique_ptr<TFile> fin = inputTools::openFile(path, "UPDATE");
  gROOT->cd();

  // Read the inputs: counts (pT binning, attached to the file) and the MPF decomposition.
  std::map<std::pair<std::string, std::string>, TH1*> hs;
  std::map<std::tuple<std::string, std::string, std::string>, Points> gs;
  for (const std::string& d : cfg.readDirs) {
    for (const std::string& m : cfg.readMethods) {
      for (const std::string& s : samples) {
        if (mcOnly.count(s) && d != "mc") continue;
        const int ref = cfg.alphaRef.count(s) ? cfg.alphaRef.at(s) : cfg.alphaRefDefault;
        if (m == "counts") {
          const std::string sample = cfg.countsFrom.count(s) ? cfg.countsFrom.at(s) : s;
          const std::string name =
              formatTemplate(cfg.inputName, {{"dir", d}, {"eta", eta}, {"method", m}, {"sample", sample}, {"ref", ref}});
          hs[{d, s}] = inputTools::get<TH1>(*fin, name, path);
        } else {
          const std::string name =
              formatTemplate(cfg.inputName, {{"dir", d}, {"eta", eta}, {"method", m}, {"sample", s}, {"ref", ref}});
          gs[{d, m, s}] = readPoints(*fin, name, path);
        }
      }
    }
  }

  // The four HDM functions, created in the order of the macro.
  const auto make = [&](const Function& f) {
    return inputTools::makeFunction(f.name, f.formula, f.xmin, f.xmax, inputTools::settingsWhere(kClass, epoch_));
  };
  const std::unique_ptr<TF1> fMpf = make(cfg.mpf), fPtbal = make(cfg.ptbal), fMpfMultijet = make(cfg.mpfMultijet),
                             fPtbalMultijet = make(cfg.ptbalMultijet);

  for (const std::string& d : cfg.writeDirs) {
    for (const std::string& m : cfg.methods) {
      for (const std::string& s : samples) {
        const bool flavorMc = mcOnly.count(s) != 0;
        const std::string src = flavorMc ? "mc" : "data";  // placeholder for missing data
        TH1* h = hs.at({src, s});
        const Points& g0 = gs.at({src, "mpfchs1", s});
        const Points& g1 = gs.at({src, "mpf1", s});
        const Points& gn = gs.at({src, "mpfn", s});
        const Points& gu = gs.at({src, "mpfu", s});
        const Points& h0 = gs.at({"mc", "mpfchs1", s});
        const Points& h1 = gs.at({"mc", "mpf1", s});
        const Points& hn = gs.at({"mc", "mpfn", s});
        const Points& hu = gs.at({"mc", "mpfu", s});
        Points gm, gp;
        if (flavorMc) {  // placeholder for the missing data ratio: MC with y = 1
          gm = gs.at({"mc", "mpfchs1", s});
          gp = gs.at({"mc", "ptchs", s});
          gm.y.assign(gm.x.size(), 1.0);
          gp.y.assign(gp.x.size(), 1.0);
        } else {
          gm = gs.at({"ratio", "mpfchs1", s});
          gp = gs.at({"ratio", "ptchs", s});
        }

        // Clones are attached to gROOT (the current directory), as in the macro.
        std::unique_ptr<TH1> hdm(static_cast<TH1*>(h->Clone(cfg.hdmClone.c_str())));
        hdm->Reset();
        std::vector<std::unique_ptr<TH1>> fsr;
        for (const auto& [clone, suffix] : cfg.fsrComponents) {
          fsr.emplace_back(static_cast<TH1*>(h->Clone(clone.c_str())));
          fsr.back()->Reset();
        }
        TH1* hfsr = fsr[0].get();

        for (int i = 1; i != h->GetNbinsX() + 1; ++i) {
          const double center = h->GetBinCenter(i), ptmin = h->GetBinLowEdge(i), ptmax = h->GetBinLowEdge(i + 1);
          const double mpf = firstInBin(ptmin, ptmax, gm).first;
          const double ptbal = firstInBin(ptmin, ptmax, gp).first;
          double q0, eq0, q1, eq1, qn, eqn, qu, equ, r0, er0, r1, er1, rn, ern, ru, eru;
          std::tie(q0, eq0) = firstInBin(ptmin, ptmax, h0);
          std::tie(q1, eq1) = firstInBin(ptmin, ptmax, h1);
          std::tie(qn, eqn) = firstInBin(ptmin, ptmax, hn);
          std::tie(qu, equ) = firstInBin(ptmin, ptmax, hu);
          std::tie(r0, er0) = firstInBin(ptmin, ptmax, g0);
          std::tie(r1, er1) = firstInBin(ptmin, ptmax, g1);
          std::tie(rn, ern) = firstInBin(ptmin, ptmax, gn);
          std::tie(ru, eru) = firstInBin(ptmin, ptmax, gu);
          if (q0 == 0 && r0 == 0 && m == "mpfchs1") continue;
          if (q1 == 0 && r1 == 0 && m == "ptchs") continue;
          if (verbose_) {
            std::printf("%s %s pt=%g\n", s.c_str(), m.c_str(), center);
            if (std::fabs(r1 + rn + ru - r0) > cfg.sumRuleWarning) {
              std::printf("i=%d pt=%g r1+rn+ru-r0=%g\n", i, center, r1 + rn + ru - r0);
            }
            if (std::fabs(q1 + qn + qu - q0) > cfg.sumRuleWarning) {
              std::printf("i=%d pt=%g q1+qn+qu-q0=%g\n", i, center, q1 + qn + qu - q0);
            }
          }

          const bool isMultijet = s == "multijet";
          const ModelParameters& pars = isMultijet ? cfg.modelMultijet : cfg.modelDefault;
          const double rnD = pars.rn, rnM = pars.rn, ruD = pars.ru, ruM = pars.ru, r2 = pars.r2;

          // The HDM model solved for R1 with the given Rn, Ru; data and MC differ in the
          // measured inputs.
          TF1* function;
          std::function<std::vector<double>(double, double)> dataParameters, mcParameters;
          if (isMultijet) {
            // turn <Rlead>/<Rrecoil> back into A = (R0-1)/(R0+1) and B = (R1-1)/(R1+1)
            const double measD = m == "mpfchs1" ? (r0 - 1) / (r0 + 1) : (r1 - 1) / (r1 + 1);
            const double measM = m == "mpfchs1" ? (q0 - 1) / (q0 + 1) : (q1 - 1) / (q1 + 1);
            const double pnd = cfg.k * rn / (2 + rn), pud = cfg.k * ru / (2 + ru);
            const double pnm = cfg.k * qn / (2 + qn), pum = cfg.k * qu / (2 + qu);
            function = m == "mpfchs1" ? fMpfMultijet.get() : fPtbalMultijet.get();
            dataParameters = [=](double rnx, double rux) { return std::vector<double>{measD, pnd, pud, rnx, rux, r2}; };
            mcParameters = [=](double rnx, double rux) { return std::vector<double>{measM, pnm, pum, rnx, rux, r2}; };
          } else {
            const double measD = m == "mpfchs1" ? r0 : r1, measM = m == "mpfchs1" ? q0 : q1;
            function = m == "mpfchs1" ? fMpf.get() : fPtbal.get();
            dataParameters = [=](double rnx, double rux) { return std::vector<double>{measD, rn, ru, rnx, rux}; };
            mcParameters = [=](double rnx, double rux) { return std::vector<double>{measM, qn, qu, rnx, rux}; };
          }
          const auto rootOf = [&](const std::vector<double>& parameters) {
            function->SetParameters(parameters.data());
            return function->GetX(0, cfg.rootMin, cfg.rootMax);
          };
          const double rd = rootOf(dataParameters(rnD, ruD));
          const double rm = rootOf(mcParameters(rnM, ruM));
          const double rdN1 = rootOf(dataParameters(rnD + cfg.dRnd, ruD));
          const double rdN2 = rootOf(dataParameters(rnD + cfg.dRnm, ruD));
          const double rmN2 = rootOf(mcParameters(rnM + cfg.dRnm, ruM));
          const double rdU1 = rootOf(dataParameters(rnD, ruD + cfg.dRud));
          const double rdU2 = rootOf(dataParameters(rnD, ruD + cfg.dRum));
          const double rmU2 = rootOf(mcParameters(rnM, ruM + cfg.dRum));

          const double rRefD = m == "mpfchs1" ? r0 : r1, rRefM = m == "mpfchs1" ? q0 : q1;
          const double dR = rd / rm - rRefD / rRefM;
          const double dRd = rd - rRefD;
          const double dRm = rm - rRefM;
          const double dRn1 = (rdN1 - rd) / rm;
          const double dRn2 = rdN2 / rmN2 - rd / rm;
          const double dRu1 = (rdU1 - rd) / rm;
          const double dRu2 = rdU2 / rmU2 - rd / rm;
          const double dR0s = std::sqrt(er0 * er0 + eq0 * eq0);
          const double dR1s = std::sqrt(er1 * er1 + eq1 * eq1);
          double dRns, dRus;
          if (m == "ptchs" && !isMultijet) {  // pT balance of Z/gamma+jet: no "- 1"
            dRns = rm / rnM * std::sqrt(ern * ern + eqn * eqn);
            dRus = rm / ruM * std::sqrt(eru * eru + equ * equ);
          } else {
            dRns = (rm / rnM - 1) * std::sqrt(ern * ern + eqn * eqn);
            dRus = (rm / ruM - 1) * std::sqrt(eru * eru + equ * equ);
          }

          const double mpfDiff = std::fabs(r0 / q0 - mpf);
          const auto fail = [&]() {
            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "softRad %s %s %s %s bin %d: r0/q0=%g vs mpf=%g", epoch_.name.c_str(),
                          d.c_str(), m.c_str(), s.c_str(), i, r0 / q0, mpf);
            return JecSysError(buffer);
          };
          if (isMultijet) {
            if (mpfDiff > cfg.multijetMpf) throw fail();
          } else if (m == "mpfchs1") {
            if (mpfDiff > cfg.zjetMpfWarning && !(mpfDiff < cfg.zjetMpf || mpf == 0)) throw fail();
          } else if (verbose_ && std::fabs(r1 / q1 - ptbal) > cfg.ptbalWarning) {
            std::printf("*i=%d pt=%g r1/q1=%g mpf=%g\n", i, center, r1 / q1, mpf);
          }

          const double ddR = std::sqrt(dRn1 * dRn1 + dRn2 * dRn2 + dRu1 * dRu1 + dRu2 * dRu2);

          if (d == "data") hdm->SetBinContent(i, rd);
          if (d == "data") hdm->SetBinError(i, er0);
          if (rm > 0 || rm < 0) {  // safety against inf/nan
            if (d == "ratio") hdm->SetBinContent(i, rd / rm);
            if (d == "mc") hdm->SetBinContent(i, rm);
            if (d == "ratio") hdm->SetBinError(i, std::sqrt(er0 * er0 + eq0 * eq0));
            if (d == "mc") hdm->SetBinError(i, eq0);
          }

          if (d == "data") hfsr->SetBinContent(i, dRd);
          // the macro tests dR_m for multijet MPF and Z/gamma+jet pT balance, dR otherwise
          const double guard = (isMultijet == (m == "mpfchs1")) ? dRm : dR;
          if (guard > 0 || guard < 0) {  // safety against inf/nan
            if (d == "ratio") hfsr->SetBinContent(i, dR);
            if (d == "mc") hfsr->SetBinContent(i, dRm);
            hfsr->SetBinError(i, ddR);
            const double values[] = {dR0s, dR1s, dRn1, dRn2, dRns, dRu1, dRu2, dRus};
            for (std::size_t c = 0; c != 8; ++c) fsr[c + 1]->SetBinContent(i, values[c]);
          }
        }

        const std::string fsrDir = formatTemplate(cfg.fsrDirName, {{"dir", d}, {"eta", eta}});
        fin->mkdir(fsrDir.c_str(), "", true);
        fin->cd(fsrDir.c_str());
        for (std::size_t c = 0; c != fsr.size(); ++c) {
          const std::string key =
              formatTemplate(cfg.fsrName, {{"method", m}, {"sample", s}, {"suffix", cfg.fsrComponents[c].second}});
          fsr[c]->Write(key.c_str(), TObject::kOverwrite);
        }
        fin->cd(formatTemplate(cfg.hdmDirName, {{"dir", d}, {"eta", eta}}).c_str());
        hdm->Write(formatTemplate(cfg.hdmName, {{"method", m}, {"sample", s}}).c_str(), TObject::kOverwrite);
        gROOT->cd();
      }
    }
  }
  fin->Close();
  gROOT->cd();
}
