#include "Reprocess.h"

#include <cstdio>

#include "GraphMaker.h"
#include "JecBands.h"
#include "JecSysError.h"
#include "TFile.h"
#include "TH1D.h"
#include "TH2.h"
#include "TProfile.h"
#include "TROOT.h"
#include "TextTools.h"

namespace {
const char* kClass = "Reprocess";
}

Reprocess::Reprocess(const Inputs& inputs, const Epoch& epoch, const std::string& jecInversion)
    : epoch_(epoch),
      settings_(ReprocessSettings::parse(inputTools::loadSettings(kClass, epoch), inputTools::settingsWhere(kClass, epoch),
                                         inputs)),
      samples_(settings_, inputTools::settingsWhere(kClass, epoch)),
      style_(inputTools::loadSettings("TdrStyle", epoch), inputTools::settingsWhere("TdrStyle", epoch)) {
  if (epoch.corLevel != "L1L2L3Res") {
    throw JecSysError("reprocess: corLevel " + epoch.corLevel + " of " + epoch.name + " is not supported");
  }
  if (!jecInversion.empty()) {
    JecInverter::parseMode(jecInversion);  // checks the name
    settings_.inversion = jecInversion;
  }
}

Reprocess::~Reprocess() = default;

TFile& Reprocess::open(const std::string& path) {
  files_.push_back(inputTools::openFile(path));  // becomes the current directory, as 'new TFile'
  return *files_.back();
}

void Reprocess::openInputs() {
  const ReprocessSettings::InputFiles& in = settings_.inputs;
  inputs_["multijet"] = &open(in.multijet);
  inputs_["incjet"] = &open(in.incjet);
  inputs_["pfjet_data"] = &open(in.pfjetData);
  inputs_["pfjet_mc"] = &open(in.pfjetMc);
  inputs_["hadw"] = &open(in.hadw);
  inputs_["gamjet"] = &open(in.gamjet);
  TFile& zmm = open(in.zmmjet);
  TFile& zee = open(in.zeejet);
  for (const auto& [key, file] : {std::pair<const char*, TFile*>{"zmmjet", &zmm}, {"zeejet", &zee}}) {
    if (!file->cd(in.zllDirectory.c_str())) {
      throw JecSysError(std::string(file->GetName()) + ": no directory " + in.zllDirectory);
    }
    inputs_[key] = file->GetDirectory(in.zllDirectory.c_str());
  }
  inputs_["zjet"] = &open(in.zjet);
}

void Reprocess::loadMassHistograms() {
  // As the macro, while the zjet file is the current directory: the profiles and their
  // projections belong to it.
  const ReprocessSettings& cfg = settings_;
  TDirectory& zee = *inputs_.at("zeejet");
  TDirectory& zmm = *inputs_.at("zmmjet");
  TDirectory& fz = *inputs_.at("zjet");
  const auto zll = [&](TDirectory& dir, const std::string& tier, int alpha) {
    const std::string name = textTools::formatTemplate(
        cfg.massZll.at(tier), {{"alpha", alpha}, {"eta", cfg.massEtaBin}, {"corLevel", epoch_.corLevel}});
    TObject* object = dir.Get(name.c_str());
    if (!object) throw JecSysError(std::string(dir.GetName()) + ": no " + name);
    if (!object->InheritsFrom(TH1::Class())) ownedMass_.emplace_back(object);
    return object;
  };
  const auto zjetProfile = [&](const std::string& tier, const std::string& alpha, const char* name) -> TObject* {
    for (const std::string& candidate : cfg.massZjet2D.at(alpha)) {
      if (auto* h2 = dynamic_cast<TH2*>(fz.Get((tier + "/" + cfg.massEtaBin + "/" + candidate).c_str()))) {
        return h2->ProfileX()->ProjectionX(name);
      }
    }
    throw JecSysError(std::string(fz.GetName()) + ": none of the zjet2D histograms for alpha " + alpha + " in " +
                      tier + "/" + cfg.massEtaBin);
  };
  auto& m = mass_;
  m["zee_ratio_30"] = zll(zee, "ratio", 30);
  m["zmm_ratio_30"] = zll(zmm, "ratio", 30);
  m["z_data_30"] = zjetProfile("data", "30", "hmz_dt");
  m["zee_data_30"] = zll(zee, "data", 30);
  m["zmm_data_30"] = zll(zmm, "data", 30);
  m["z_mc_30"] = zjetProfile("mc", "30", "hmz_mc");
  m["zee_mc_30"] = zll(zee, "mc", 30);
  m["zmm_mc_30"] = zll(zmm, "mc", 30);
  m["zee_ratio_100"] = zll(zee, "ratio", 100);
  m["zmm_ratio_100"] = zll(zmm, "ratio", 100);
  m["z_data_100"] = zjetProfile("data", "100", "hmz1_dt");
  m["zee_data_100"] = zll(zee, "data", 100);
  m["zmm_data_100"] = zll(zmm, "data", 100);
  m["z_mc_100"] = zjetProfile("mc", "100", "hmz1_mc");
  m["zee_mc_100"] = zll(zee, "mc", 100);
  m["zmm_mc_100"] = zll(zmm, "mc", 100);
  auto* ratio30 = static_cast<TH1*>(m["z_data_30"]->Clone("hmz"));
  ratio30->Divide(static_cast<TH1*>(m["z_mc_30"]));
  m["z_ratio_30"] = ratio30;
  auto* ratio100 = static_cast<TH1*>(m["z_data_100"]->Clone("hmz1"));
  ratio100->Divide(static_cast<TH1*>(m["z_mc_100"]));
  m["z_ratio_100"] = ratio100;
}

void Reprocess::writeMassHistograms(TFile& out) {
  std::printf("Save mass histos\n");
  for (const char* tier : {"ratio", "data", "mc"}) {
    out.cd((std::string(tier) + "/eta00-13").c_str());
    for (const char* alpha : {"30", "100"}) {
      for (const auto& [key, sample] :
           {std::pair<const char*, const char*>{"zee", "zeejet"}, {"zmm", "zmmjet"}, {"z", "zjet"}}) {
        mass_.at(std::string(key) + "_" + tier + "_" + alpha)
            ->Write(("mass_" + std::string(sample) + "_a" + alpha).c_str());
      }
    }
  }
}

void Reprocess::run(const std::string& path) {
  // The first setTDRStyle() call of a fresh ROOT session, as reprocess.C (also when an
  // earlier epoch of the same process changed the style).
  style_.setAsFirst();
  textTools::makeDirs(textTools::dirName(path));
  std::unique_ptr<TFile> fout(TFile::Open(path.c_str(), "RECREATE"));
  if (!fout || fout->IsZombie()) throw JecSysError("Cannot create " + path);
  openInputs();
  loadMassHistograms();
  GraphMaker graphs(settings_, samples_, epoch_.corLevel, epoch_.name);
  graphs.makeCorrectionFunctions();
  TFile& weights = open(settings_.inputs.etaWeights);
  auto* h2pteta = dynamic_cast<TH2*>(weights.Get(settings_.inputs.etaWeightsObject.c_str()));
  if (!h2pteta) throw JecSysError(settings_.inputs.etaWeights + ": no TH2 " + settings_.inputs.etaWeightsObject);
  graphs.setInputs(inputs_);
  graphs.makeAll(*fout);
  writeMassHistograms(*fout);
  for (const char* key : {"gamjet", "zeejet", "zmmjet", "zjet"}) inputs_.at(key)->Close();  // as the macro
  mass_.clear();
  gROOT->cd();
  JecBands(settings_, epoch_.corLevel, epoch_.name, JecInverter::parseMode(settings_.inversion)).make(*fout, *h2pteta);
  fout->Close();
  for (auto it = files_.rbegin(); it != files_.rend(); ++it) (*it)->Close();
  files_.clear();
  inputs_.clear();
  ownedMass_.clear();
  gROOT->cd();
}
