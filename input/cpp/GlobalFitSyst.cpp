#include "GlobalFitSyst.h"

#include <cmath>

#include "JecSysError.h"
#include "TF1.h"
#include "TFile.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TMatrixD.h"
#include "TProfile.h"
#include "TROOT.h"
#include "TVectorD.h"
#include "TextTools.h"

namespace {

const char* kClass = "GlobalFitSyst";

using Settings = GlobalFitSyst::Settings;

std::map<std::string, GlobalFitSyst::Function> parseFunctions(const SettingsReader& block) {
  std::map<std::string, GlobalFitSyst::Function> functions;
  const nlohmann::json& list = block.get<nlohmann::json>("functions");
  if (!list.is_object()) throw JecSysError(block.where() + ": 'functions' must be an object");
  for (auto it = list.begin(); it != list.end(); ++it) {
    const SettingsReader f(it.value(), block.where() + ".functions." + it.key(), {"formula", "range", "parameters"});
    const auto [xmin, xmax] = f.range("range");
    functions[it.key()] = {f.get<std::string>("formula"), xmin, xmax, f.get<std::vector<double>>("parameters")};
  }
  return functions;
}

// new TF1(name, formula, range) with the parameters of the configuration.
std::unique_ptr<TF1> makeFunction(const std::string& name, const GlobalFitSyst::Function& spec,
                                  const std::string& where) {
  auto function = inputTools::makeFunction(name, spec.formula, spec.xmin, spec.xmax, where);
  if (static_cast<int>(spec.parameters.size()) != function->GetNpar()) {
    throw JecSysError(where + ": " + name + " has " + std::to_string(function->GetNpar()) + " parameters, " +
                      std::to_string(spec.parameters.size()) + " given");
  }
  function->SetParameters(spec.parameters.data());
  return function;
}

// hist->Clone(name) (attached to the current directory), emptied.
std::unique_ptr<TH1> cloneReset(const TH1& hist, const std::string& name) {
  std::unique_ptr<TH1> clone(static_cast<TH1*>(hist.Clone(name.c_str())));
  clone->Reset();
  return clone;
}

}  // namespace

Settings Settings::parse(const nlohmann::json& json, const std::string& where, const Inputs& inputs) {
  const SettingsReader r(json, where, {"etaDir", "fsrDir", "sysDir", "multijetJer", "gamMass", "gamGains",
                                       "hadwFitProb", "hadwFsr", "incjetHdm"});
  Settings s;
  s.etaDir = r.get<std::string>("etaDir");
  s.fsrDir = r.get<std::string>("fsrDir");
  s.sysDir = r.get<std::string>("sysDir");

  const SettingsReader jer =
      r.child("multijetJer", {"enabled", "mode", "binning", "clone", "output", "ptRange", "scale", "functions"});
  MultijetJer& j = s.multijetJer;
  j.enabled = jer.get<bool>("enabled");
  j.mode = jer.get<std::string>("mode");
  if (j.mode != "leading" && j.mode != "recoil" && j.mode != "ptave") {
    throw JecSysError(jer.where() + ": mode must be leading, recoil or ptave");
  }
  j.binning = jer.get<std::string>("binning");
  j.clone = jer.get<std::string>("clone");
  j.output = jer.get<std::string>("output");
  std::tie(j.ptMin, j.ptMax) = jer.range("ptRange");
  j.scale = jer.get<double>("scale");
  j.functions = parseFunctions(jer);
  for (const char* key : {"leading", "recoil"}) {
    if (!j.functions.count(key)) throw JecSysError(jer.where() + ": functions needs '" + key + "'");
  }

  const SettingsReader mass =
      r.child("gamMass", {"enabled", "binning", "formula", "range", "errorMatrix", "ptFactor", "output"});
  GamMass& m = s.gamMass;
  m.enabled = mass.get<bool>("enabled");
  m.binning = mass.get<std::string>("binning");
  m.formula = mass.get<std::string>("formula");
  std::tie(m.xmin, m.xmax) = mass.range("range");
  m.errorMatrix = mass.get<std::vector<std::vector<double>>>("errorMatrix");
  for (std::size_t i = 0; i != m.errorMatrix.size(); ++i) {
    if (m.errorMatrix[i].size() != m.errorMatrix.size()) throw JecSysError(mass.where() + ": errorMatrix is not square");
    for (std::size_t k = 0; k != i; ++k) {
      if (m.errorMatrix[i][k] != m.errorMatrix[k][i]) throw JecSysError(mass.where() + ": errorMatrix is not symmetric");
    }
  }
  m.ptFactor = mass.get<double>("ptFactor");
  m.output = mass.get<std::string>("output");

  const SettingsReader gains = r.child("gamGains", {"enabled", "file", "gain1", "gain6", "scale", "hdm"});
  GamGains& g = s.gamGains;
  g.enabled = gains.get<bool>("enabled");
  g.file = inputs.resolvePath(gains.get<std::string>("file"));
  const SettingsReader gain1 = gains.child("gain1", {"profile", "output"});
  const SettingsReader gain6 = gains.child("gain6", {"profile", "output"});
  g.gain1Profile = gain1.get<std::string>("profile");
  g.gain1Output = gain1.get<std::string>("output");
  g.gain6Profile = gain6.get<std::string>("profile");
  g.gain6Output = gain6.get<std::string>("output");
  g.scale = gains.get<double>("scale");
  const SettingsReader hdm = gains.child("hdm", {"source", "output", "gain1Weight", "gain6Weight"});
  g.hdmSource = hdm.get<std::string>("source");
  g.hdmOutput = hdm.get<std::string>("output");
  g.gain1Weight = hdm.get<double>("gain1Weight");
  g.gain6Weight = hdm.get<double>("gain6Weight");

  const SettingsReader fitProb = r.child("hadwFitProb", {"enabled", "binning", "scale", "functions", "outputs"});
  HadwFitProb& f = s.hadwFitProb;
  f.enabled = fitProb.get<bool>("enabled");
  f.binning = fitProb.get<std::string>("binning");
  f.scale = fitProb.get<double>("scale");
  f.functions = parseFunctions(fitProb);
  for (const auto& entry : fitProb.get<std::vector<nlohmann::json>>("outputs")) {
    const SettingsReader o(entry, fitProb.where() + ".outputs", {"name", "function", "minus"});
    HadwFitProb::Output output{o.get<std::string>("name"), o.get<std::string>("function"),
                               o.has("minus") ? o.get<std::string>("minus") : ""};
    for (const std::string& function : {output.function, output.minus}) {
      if (!function.empty() && !f.functions.count(function)) {
        throw JecSysError(o.where() + ": unknown function '" + function + "'");
      }
    }
    f.outputs.push_back(output);
  }

  const SettingsReader fsr = r.child("hadwFsr", {"enabled", "file", "nominal", "up", "down", "fsr", "fsrScale", "graph",
                                                 "graphClone", "graphOutput", "histOutput", "sysOutputs"});
  HadwFsr& h = s.hadwFsr;
  h.enabled = fsr.get<bool>("enabled");
  h.file = inputs.resolvePath(fsr.get<std::string>("file"));
  h.nominal = fsr.get<std::string>("nominal");
  h.up = fsr.get<std::string>("up");
  h.down = fsr.get<std::string>("down");
  h.fsr = fsr.get<std::string>("fsr");
  h.fsrScale = fsr.get<double>("fsrScale");
  h.graph = fsr.get<std::string>("graph");
  h.graphClone = fsr.get<std::string>("graphClone");
  h.graphOutput = fsr.get<std::string>("graphOutput");
  h.histOutput = fsr.get<std::string>("histOutput");
  h.sysOutputs = fsr.get<std::vector<std::string>>("sysOutputs");

  const SettingsReader incjet =
      r.child("incjetHdm", {"enabled", "referenceFile", "reference", "graph", "graphOutput", "histOutput"});
  IncjetHdm& i = s.incjetHdm;
  i.enabled = incjet.get<bool>("enabled");
  i.referenceFile = inputs.resolvePath(incjet.get<std::string>("referenceFile"));
  i.reference = incjet.get<std::string>("reference");
  i.graph = incjet.get<std::string>("graph");
  i.graphOutput = incjet.get<std::string>("graphOutput");
  i.histOutput = incjet.get<std::string>("histOutput");
  return s;
}

GlobalFitSyst::GlobalFitSyst(const Inputs& inputs, const Epoch& epoch)
    : epoch_(epoch),
      settings_(Settings::parse(inputTools::loadSettings(kClass, epoch), inputTools::settingsWhere(kClass, epoch),
                                inputs)),
      style_(inputTools::loadSettings("TdrStyle", epoch), inputTools::settingsWhere("TdrStyle", epoch)) {}

void GlobalFitSyst::run(const std::string& path) {
  const Settings& cfg = settings_;
  const std::string where = inputTools::settingsWhere(kClass, epoch_);
  inputTools::requireFile(path, name());
  style_.setAsInChain();
  TDirectory* curdir = gDirectory;
  std::unique_ptr<TFile> fout = inputTools::openFile(path, "UPDATE");
  if (!fout->cd(textTools::split(cfg.etaDir, '/')[0].c_str())) throw JecSysError(path + ": no directory " + cfg.etaDir);
  TDirectory* d2 = fout->GetDirectory(cfg.etaDir.c_str());
  if (!d2 || !d2->cd(cfg.fsrDir.c_str())) {
    throw JecSysError(path + ": no directory " + cfg.etaDir + "/" + cfg.fsrDir + " (run softRad first)");
  }
  TDirectory* dfsr = d2->GetDirectory(cfg.fsrDir.c_str());
  if (!d2->FindObject(cfg.sysDir.c_str())) d2->mkdir(cfg.sysDir.c_str());
  if (!d2->cd(cfg.sysDir.c_str())) throw JecSysError(path + ": cannot create " + cfg.etaDir + "/" + cfg.sysDir);
  TDirectory* dsys = d2->GetDirectory(cfg.sysDir.c_str());
  dsys->cd();
  curdir->cd();

  // Input files stay open until the pre-fit file is closed, as in the macro: the
  // projections made while they were the current directory belong to them.
  std::vector<std::unique_ptr<TFile>> openFiles;

  if (cfg.multijetJer.enabled) {
    const MultijetJer& c = cfg.multijetJer;
    const std::unique_ptr<TF1> fRecoil = makeFunction("frecoil", c.functions.at("recoil"), where);
    const std::unique_ptr<TF1> fLeading = makeFunction("fleading", c.functions.at("leading"), where);
    TH1* hist = inputTools::get<TH1>(*d2, c.binning, path);
    const std::unique_ptr<TH1> out = cloneReset(*hist, c.clone);
    for (int i = 1; i != hist->GetNbinsX() + 1; ++i) {
      const double pt = hist->GetBinCenter(i);
      if (c.ptMin < pt && pt < c.ptMax) {
        if (c.mode == "leading") {
          out->SetBinContent(i, c.scale * fLeading->Eval(pt));
        } else if (c.mode == "recoil") {
          out->SetBinContent(i, c.scale * fRecoil->Eval(pt));
        } else {
          out->SetBinContent(i, c.scale * 0.5 * (fLeading->Eval(pt) + fRecoil->Eval(pt)));
        }
      }
    }
    dsys->cd();
    out->Write(c.output.c_str(), TObject::kOverwrite);
    curdir->cd();
  }

  if (cfg.gamMass.enabled) {
    const GamMass& c = cfg.gamMass;
    TH1* hk = inputTools::get<TH1>(*d2, c.binning, path);
    const int n = static_cast<int>(c.errorMatrix.size());
    TMatrixD emat(n, n);
    for (int i = 0; i != n; ++i) {
      for (int k = 0; k != n; ++k) emat[i][k] = c.errorMatrix[i][k];
    }
    TVectorD eigvec(n);
    const TMatrixD eigmat = emat.EigenVectors(eigvec);
    const std::unique_ptr<TF1> fkeig = inputTools::makeFunction("fkeig", c.formula, c.xmin, c.xmax, where);
    for (int ieig = 0; ieig != n; ++ieig) {
      for (int i = 0; i != n; ++i) fkeig->SetParameter(i, eigmat[i][ieig] * std::sqrt(eigvec[ieig]));
      const std::unique_ptr<TH1> hke = cloneReset(*hk, textTools::formatTemplate(c.output, {{"eig", ieig}}));
      for (int i = 1; i != hk->GetNbinsX() + 1; ++i) {
        const double pt = hk->GetBinCenter(i);
        hke->SetBinContent(i, fkeig->Eval(c.ptFactor * pt));
      }
      dsys->cd();
      hke->Write(hke->GetName(), TObject::kOverwrite);
      curdir->cd();
    }
  }

  if (cfg.gamGains.enabled) {
    const GamGains& c = cfg.gamGains;
    openFiles.push_back(inputTools::openFile(c.file));  // now the current directory
    TFile& fgain = *openFiles.back();
    TH1* h1 = inputTools::get<TProfile>(fgain, c.gain1Profile, c.file)->ProjectionX(c.gain1Output.c_str());
    h1->Scale(c.scale);
    TH1* h6 = inputTools::get<TProfile>(fgain, c.gain6Profile, c.file)->ProjectionX(c.gain6Output.c_str());
    h6->Scale(c.scale);
    dsys->cd();
    h1->Write(h1->GetName(), TObject::kOverwrite);
    h6->Write(h6->GetName(), TObject::kOverwrite);
    curdir->cd();

    const std::unique_ptr<TH1> hg(
        static_cast<TH1*>(inputTools::get<TH1>(*d2, c.hdmSource, path)->Clone(c.hdmOutput.c_str())));
    for (int i = 1; i != hg->GetNbinsX() + 1; ++i) {
      if (hg->GetBinContent(i) != 0) {
        const int j = h1->FindBin(hg->GetBinCenter(i));
        hg->SetBinContent(i, hg->GetBinContent(i) - c.gain1Weight * h1->GetBinContent(j) -
                                 c.gain6Weight * h6->GetBinContent(j));
      }
    }
    d2->cd();
    hg->Write(hg->GetName(), TObject::kOverwrite);
    curdir->cd();
  }

  if (cfg.hadwFitProb.enabled) {
    const HadwFitProb& c = cfg.hadwFitProb;
    TH1* hk = inputTools::get<TH1>(*d2, c.binning, path);
    std::map<std::string, std::unique_ptr<TF1>> functions;
    for (const auto& [name, spec] : c.functions) functions[name] = makeFunction(name, spec, where);
    std::vector<std::unique_ptr<TH1>> outputs;
    for (const HadwFitProb::Output& spec : c.outputs) {
      outputs.push_back(cloneReset(*hk, spec.name));
      TH1* hist = outputs.back().get();
      TF1* main = functions.at(spec.function).get();
      TF1* minus = spec.minus.empty() ? nullptr : functions.at(spec.minus).get();
      for (int i = 1; i != hist->GetNbinsX() + 1; ++i) {
        const double pt = hist->GetBinCenter(i);
        hist->SetBinContent(i, minus ? c.scale * (main->Eval(pt) - minus->Eval(pt)) : c.scale * main->Eval(pt));
      }
    }
    dsys->cd();
    for (std::size_t k = 0; k != outputs.size(); ++k) outputs[k]->Write(c.outputs[k].name.c_str(), TObject::kOverwrite);
    curdir->cd();
  }

  if (cfg.hadwFsr.enabled) {
    const HadwFsr& c = cfg.hadwFsr;
    openFiles.push_back(inputTools::openFile(c.file));  // now the current directory
    TFile& ffsr = *openFiles.back();
    TProfile* pmc = inputTools::get<TProfile>(ffsr, c.nominal, c.file);
    TProfile* pup = inputTools::get<TProfile>(ffsr, c.up, c.file);
    TProfile* pdw = inputTools::get<TProfile>(ffsr, c.down, c.file);

    TH1D* hfsr = pup->ProjectionX(c.fsr.c_str());  // belongs to ffsr
    hfsr->Add(pdw, -1);
    hfsr->Scale(c.fsrScale);
    hfsr->Add(pmc, +1);
    hfsr->Divide(pmc);
    dfsr->cd();
    hfsr->Write(c.fsr.c_str(), TObject::kOverwrite);
    curdir->cd();

    const std::unique_ptr<TGraphErrors> graph(inputTools::get<TGraphErrors>(*d2, c.graph, path));
    const std::unique_ptr<TGraphErrors> gw(static_cast<TGraphErrors*>(graph->Clone(c.graphClone.c_str())));
    const std::unique_ptr<TH1> hw = cloneReset(*hfsr, c.histOutput);
    for (int i = 0; i != gw->GetN(); ++i) {
      const double pt = gw->GetX()[i];
      const double fsr = hfsr->GetBinContent(hfsr->FindBin(pt));
      const double newhadw = gw->GetY()[i] / fsr;
      gw->SetPoint(i, pt, newhadw);
      const int j = hw->FindBin(pt);
      hw->SetBinContent(j, newhadw);
      hw->SetBinError(j, gw->GetEY()[i] / fsr);
    }
    d2->cd();
    gw->Write(c.graphOutput.c_str(), TObject::kOverwrite);
    hw->Write(c.histOutput.c_str(), TObject::kOverwrite);
    curdir->cd();

    std::vector<std::unique_ptr<TH1>> sysHists;  // attached to curdir
    for (const std::string& name : c.sysOutputs) {
      sysHists.emplace_back(pup->ProjectionX(name.c_str()));
      sysHists.back()->Add(pdw, -1);
      sysHists.back()->Divide(pmc);
    }
    dsys->cd();
    for (std::size_t k = 0; k != sysHists.size(); ++k) sysHists[k]->Write(c.sysOutputs[k].c_str(), TObject::kOverwrite);
    curdir->cd();
  }

  if (cfg.incjetHdm.enabled) {
    const IncjetHdm& c = cfg.incjetHdm;
    std::unique_ptr<TFile> fref = inputTools::openFile(c.referenceFile);  // now the current directory
    TH1* href = inputTools::get<TH1>(*fref, c.reference, c.referenceFile);
    const std::unique_ptr<TGraphErrors> graph(inputTools::get<TGraphErrors>(*d2, c.graph, path));
    const std::unique_ptr<TGraphErrors> g(static_cast<TGraphErrors*>(graph->Clone(c.graphOutput.c_str())));
    std::vector<double> edges{g->GetX()[0] - g->GetEX()[0]};
    for (int i = 0; i != g->GetN(); ++i) edges.push_back(g->GetX()[i] + g->GetEX()[i]);
    auto* h = new TH1D(c.histOutput.c_str(), "", static_cast<int>(edges.size()) - 1, edges.data());  // belongs to fref
    for (int i = 0; i != g->GetN(); ++i) {
      const int j = h->FindBin(g->GetX()[i]);
      const double cref = href->GetBinContent(href->FindBin(g->GetX()[i]));
      h->SetBinContent(j, g->GetY()[i] * cref);
      h->SetBinError(j, g->GetEY()[i] * cref);
      g->SetPoint(i, g->GetX()[i], g->GetY()[i] * cref);
    }
    d2->cd();
    g->Write(c.graphOutput.c_str(), TObject::kOverwrite);
    h->Write(c.histOutput.c_str(), TObject::kOverwrite);
    curdir->cd();
    fref->Close();
  }

  fout->Close();
  curdir->cd();
}
