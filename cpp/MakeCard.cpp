#include "MakeCard.h"

#include <algorithm>
#include <cstdio>
#include <memory>

#include "InputFile.h"
#include "JecSysError.h"
#include "OutputFile.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TextTools.h"

namespace {

std::string etaBinName(const std::pair<double, double>& etaRange) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "eta%02.0f-%02.0f", 10 * etaRange.first, 10 * etaRange.second);
  return buffer;
}

std::string graphName(const std::string& quantity, const std::string& sample, int alpha) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "_a%02d", alpha);
  return quantity + "_" + sample + buffer;
}

// Remove points without content or without uncertainty (was globalFitL3Res.C:908).
void removeEmptyPoints(TGraphErrors& graph) {
  for (int i = graph.GetN() - 1; i != -1; --i) {
    if (graph.GetY()[i] == 0 || graph.GetEY()[i] == 0) graph.RemovePoint(i);
  }
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------------------
MakeCardConfig MakeCardConfig::fromJson(const nlohmann::json& settings, const std::string& where) {
  using Json = nlohmann::json;
  ConfigFile::checkKeys(settings,
                        {"etaRange", "ptMin", "methods", "samples", "sampleSettings",
                         "fsrEigenvectors", "sources", "pfFractions", "jesRef"},
                        where);
  MakeCardConfig config;
  const auto eta = ConfigFile::get<std::vector<double>>(settings, "etaRange", where);
  if (eta.size() != 2) throw JecSysError(where + ": etaRange must be [min, max]");
  config.etaRange = {eta[0], eta[1]};
  config.ptMin = ConfigFile::get<double>(settings, "ptMin", where);
  config.methods = ConfigFile::get<std::vector<std::string>>(settings, "methods", where);
  config.samples = ConfigFile::get<std::vector<std::string>>(settings, "samples", where);
  config.fsrEigenvectors = ConfigFile::get<std::vector<std::string>>(settings, "fsrEigenvectors", where);
  config.jesRef = ConfigFile::get<std::string>(settings, "jesRef", where);
  if (config.methods.empty() || config.samples.empty()) {
    throw JecSysError(where + ": 'methods' and 'samples' must not be empty");
  }

  const auto perSample = ConfigFile::get<Json>(settings, "sampleSettings", where);
  for (const std::string& sample : config.samples) {
    const std::string sub = where + ": sampleSettings." + sample;
    if (!perSample.contains(sample)) throw JecSysError(sub + " is missing");
    const Json& node = perSample[sample];
    ConfigFile::checkKeys(node, {"kind", "alpha", "fsr", "crecoil", "scale", "hdmScale"}, sub);
    SampleSettings s;
    s.kind = parseChannelKind(ConfigFile::get<std::string>(node, "kind", sub));
    if (s.kind == ChannelKind::PfFraction) throw JecSysError(sub + ": use 'pfFractions' for PF channels");
    s.alpha = ConfigFile::get<int>(node, "alpha", sub);
    const std::string fsr = ConfigFile::get<std::string>(node, "fsr", sub);
    if (fsr != "hdm" && fsr != "none") {
      throw JecSysError(sub + ": fsr must be 'hdm' or 'none' (the alpha extrapolation of "
                              "softrad.C is not supported)");
    }
    s.hdmFsr = fsr == "hdm";
    if (s.kind == ChannelKind::Multijet) s.crecoil = ConfigFile::get<std::string>(node, "crecoil", sub);
    if (node.contains("scale")) s.scale = ConfigFile::get<double>(node, "scale", sub);
    if (node.contains("hdmScale")) s.hdmScale = ConfigFile::get<double>(node, "hdmScale", sub);
    config.sampleSettings[sample] = s;
  }

  for (const Json& node : ConfigFile::get<Json>(settings, "sources", where)) {
    const std::string sub = where + ": sources";
    ConfigFile::checkKeys(node, {"name", "object", "from", "appliesTo"}, sub);
    ExtraSource source;
    source.name = ConfigFile::get<std::string>(node, "name", sub);
    source.object = ConfigFile::get<std::string>(node, "object", sub);
    const std::string from = ConfigFile::get<std::string>(node, "from", sub);
    if (from != "epoch" && from != "refIov") throw JecSysError(sub + ": from must be 'epoch' or 'refIov'");
    source.fromRefIov = from == "refIov";
    source.appliesTo = ConfigFile::get<std::vector<std::string>>(node, "appliesTo", sub);
    config.sources.push_back(source);
  }

  const auto pf = ConfigFile::get<Json>(settings, "pfFractions", where);
  ConfigFile::checkKeys(pf, {"fractions", "samples"}, where + ": pfFractions");
  config.pfFractions = ConfigFile::get<std::vector<std::string>>(pf, "fractions", where + ": pfFractions");
  for (const Json& node : ConfigFile::get<Json>(pf, "samples", where + ": pfFractions")) {
    const std::string sub = where + ": pfFractions.samples";
    ConfigFile::checkKeys(node, {"sample", "alpha", "delta", "ptRange"}, sub);
    PfSample s;
    s.sample = ConfigFile::get<std::string>(node, "sample", sub);
    s.alpha = ConfigFile::get<int>(node, "alpha", sub);
    s.delta = ConfigFile::get<double>(node, "delta", sub);
    const auto range = ConfigFile::get<std::vector<double>>(node, "ptRange", sub);
    if (range.size() != 2) throw JecSysError(sub + ": ptRange must be [min, max]");
    s.ptRange = {range[0], range[1]};
    config.pfSamples.push_back(s);
  }
  return config;
}

// ---------------------------------------------------------------------------------------
// MakeCard
// ---------------------------------------------------------------------------------------
MakeCard::MakeCard(MakeCardConfig config, const Inputs& inputs, std::string resolvedConfigText)
    : config_(std::move(config)), inputs_(inputs), resolvedConfigText_(std::move(resolvedConfigText)) {}

Datacard MakeCard::run(const Epoch& epoch) const {
  const std::string etaBin = etaBinName(config_.etaRange);
  const std::string dir = "ratio/" + etaBin + "/";
  const InputFile forfit(epoch.forfit);
  const std::string shapesFile = "shapes.root";  // relative to the card
  const std::string cardDir = inputs_.cardDir(epoch);

  // A source of the reference IOV is not copied: the card points to the shapes file
  // of that IOV, so the dependency is visible in the card.
  const Epoch& refEpoch = inputs_.epoch(epoch.refIov);
  const std::string refShapes = inputs_.shapesPath(refEpoch);
  if (!epoch.isRefIov() && !textTools::fileExists(refShapes)) {
    throw JecSysError("Epoch " + epoch.name + " needs the card of its reference IOV: " + refShapes +
                      " is missing. Run 'jecMakeCard -e " + refEpoch.name + "' first.");
  }

  Datacard card;
  card.setBaseDir(cardDir);
  card.headerComments = {
      "Datacard for the JEC L3Res global fit, epoch " + epoch.name + " (" + epoch.corLevel + ", " +
          etaBin + ")",
      "Written by jecMakeCard from config/MakeCard.json and " + epoch.forfit,
      "DO NOT EDIT BY HAND: change config/MakeCard.json and run jecMakeCard again.",
      "Format: doc/datacard.md",
  };
  card.epoch = epoch.name;
  card.corLevel = epoch.corLevel;
  card.etaBin = etaBin;
  card.ptMin = config_.ptMin;
  card.shapes.push_back({"data", "*", shapesFile, "data/$CHANNEL"});
  card.shapes.push_back({"*", "*", shapesFile, "nuisance/$NUISANCE"});

  OutputFile out(cardDir + "/" + shapesFile);

  // ---- response channels: data with the FSR correction applied (was lines 872-1180) ----
  struct ChannelInfo {
    std::string name, method, sample;
  };
  std::vector<ChannelInfo> responseChannels;
  for (const std::string& method : config_.methods) {
    for (const std::string& sample : config_.samples) {
      const SampleSettings& settings = config_.sampleSettings.at(sample);
      const std::string channel = method + "_" + sample;
      responseChannels.push_back({channel, method, sample});

      auto graph = forfit.get<TGraphErrors>(dir + graphName(method, sample, settings.alpha));
      removeEmptyPoints(*graph);

      if (settings.hdmFsr) {
        // HDM: the correction histogram is an additive shift dR of the response.
        const auto fsr = forfit.get<TH1D>(dir + "fsr/hkfsr3_" + method + "_" + sample);
        for (int i = 0; i != graph->GetN(); ++i) {
          const double pt = graph->GetX()[i];
          const double r = graph->GetY()[i];
          const double kfsr = (r + fsr->GetBinContent(fsr->FindBin(pt))) / r;
          graph->SetPoint(i, pt, r * kfsr);
        }
        out.write("fsr/" + channel, *fsr);
        card.aux.push_back({"fsr", channel, shapesFile, "fsr/$CHANNEL"});
      }
      graph->SetName(channel.c_str());
      out.write("data/" + channel, *graph);

      CardChannel cardChannel;
      cardChannel.name = channel;
      cardChannel.kind = settings.kind;
      card.channels.push_back(cardChannel);

      if (settings.kind == ChannelKind::Multijet) {
        const auto crecoil = forfit.get<TGraphErrors>(dir + settings.crecoil);
        if (crecoil->GetN() < graph->GetN()) {
          throw JecSysError(epoch.forfit + ": " + settings.crecoil + " has fewer points than " + channel);
        }
        out.write("aux/crecoil_" + channel, *crecoil);
        card.aux.push_back({"crecoil", channel, shapesFile, "aux/crecoil_$CHANNEL"});
      }
    }
  }
  // ---- PF-composition channels (was lines 717-863) ----
  for (const PfSample& pf : config_.pfSamples) {
    for (const std::string& fraction : config_.pfFractions) {
      const std::string channel = fraction + "_" + pf.sample;
      auto graph = forfit.get<TGraphErrors>(dir + graphName(fraction, pf.sample, pf.alpha));
      graph->SetName(channel.c_str());
      out.write("data/" + channel, *graph);
      CardChannel cardChannel;
      cardChannel.name = channel;
      cardChannel.kind = ChannelKind::PfFraction;
      cardChannel.fraction = fraction;
      cardChannel.delta = pf.delta;
      cardChannel.ptRange = pf.ptRange;
      card.channels.push_back(cardChannel);
    }
  }

  // ---- closure reference and output binning ----
  if (epoch.corLevel == "L1L2L3Res") {
    out.write("aux/jesRef", *forfit.get<TH1D>(dir + config_.jesRef));
    card.aux.push_back({"jesRef", "*", shapesFile, "aux/jesRef"});
  }
  {
    auto binning = InputFile(epoch.ptBinning.file).get<TH1D>(epoch.ptBinning.object);
    binning->Reset();
    binning->SetName("ptBinning");
    out.write("aux/ptBinning", *binning);
    card.aux.push_back({"ptBinning", "*", shapesFile, "aux/ptBinning"});
  }

  // ---- nuisances, in the order of today's fit (lines 1186-1612) ----
  const auto newNuisance = [&card](const std::string& name, CardNuisance::Type type) -> CardNuisance& {
    CardNuisance nuisance;
    nuisance.name = name;
    nuisance.type = type;
    nuisance.values.resize(card.channels.size());
    card.nuisances.push_back(nuisance);
    return card.nuisances.back();
  };

  // 1. FSR eigenvectors: one nuisance per (eigenvector, method, sample).
  CardGroup fsrGroup{"fsr", {}};
  for (const std::string& eig : config_.fsrEigenvectors) {
    for (const ChannelInfo& channel : responseChannels) {
      const std::string name = "fsr_" + eig + "_" + channel.name;
      CardNuisance& nuisance = newNuisance(name, CardNuisance::Type::Shape);
      fsrGroup.nuisances.push_back(name);
      if (!config_.sampleSettings.at(channel.sample).hdmFsr) {
        nuisance.comment = "inactive: no FSR correction for " + channel.sample;
        continue;
      }
      const auto hist =
          forfit.get<TH1D>(dir + "fsr/hkfsr3_" + channel.method + "_" + channel.sample + "_" + eig);
      hist->SetName(name.c_str());
      out.write("nuisance/" + name, *hist);
      nuisance.values[card.channelIndex(channel.name)] = 1;
    }
  }
  card.groups.push_back(fsrGroup);

  // 2. + 3. Flat scale nuisances, acting on every method of the sample.
  // As today, "scale" exists only if the pT-balance method is fitted and "hdmscale" only
  // if MPF is fitted (globalFitL3Res.C:1302-1388).
  const auto hasMethod = [this](const std::string& method) {
    return std::find(config_.methods.begin(), config_.methods.end(), method) != config_.methods.end();
  };
  const auto addFlat = [&](const std::string& prefix, std::optional<double> SampleSettings::*member,
                           const std::string& requiredMethod) {
    if (!hasMethod(requiredMethod)) return;
    for (const std::string& sample : config_.samples) {
      const std::optional<double>& value = config_.sampleSettings.at(sample).*member;
      if (!value) continue;
      CardNuisance& nuisance = newNuisance(prefix + "_" + sample, CardNuisance::Type::Flat);
      for (const ChannelInfo& channel : responseChannels) {
        if (channel.sample == sample) nuisance.values[card.channelIndex(channel.name)] = *value;
      }
    }
  };
  addFlat("scale", &SampleSettings::scale, "ptchs");
  addFlat("hdmscale", &SampleSettings::hdmScale, "mpfchs1");

  // 4. Histogram sources from the systematics step, of this epoch or of its reference IOV.
  std::unique_ptr<InputFile> refForfit;
  for (const ExtraSource& source : config_.sources) {
    std::vector<int> targets;
    for (const std::string& channel : source.appliesTo) {
      const int index = card.channelIndex(channel);
      if (index >= 0) targets.push_back(index);
    }
    if (targets.empty()) continue;  // none of its channels is in this fit
    CardNuisance& nuisance = newNuisance(source.name, CardNuisance::Type::Shape);
    for (const int index : targets) nuisance.values[index] = 1;

    if (source.fromRefIov && !epoch.isRefIov()) {
      nuisance.comment = "from refIov " + refEpoch.name;
      card.shapes.push_back({source.name, "*", textTools::relativePath(refShapes, cardDir),
                             "nuisance/$NUISANCE"});
      if (!InputFile(refShapes).has("nuisance/" + source.name)) {
        throw JecSysError(refShapes + " has no nuisance/" + source.name + "; remake the card of " +
                          refEpoch.name);
      }
    } else {
      const auto hist = forfit.get<TH1D>(dir + source.object);
      hist->SetName(source.name.c_str());
      out.write("nuisance/" + source.name, *hist);
    }
  }

  // ---- provenance ----
  out.writeText("_meta/makeCard/config", resolvedConfigText_);
  out.writeText("_meta/makeCard/forfit", epoch.forfit);
  out.writeText("_meta/makeCard/refIov", epoch.refIov);

  card.validate();  // before anything is replaced on disk
  out.commit();
  card.write(cardDir + "/datacard.txt");
  return card;
}
