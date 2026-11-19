#include "Workspace.h"

#include <map>

#include "InputFile.h"
#include "JecSysError.h"
#include "OutputFile.h"

namespace {

// Opens each input file once.
class FileCache {
 public:
  const InputFile& open(const std::string& path) {
    auto it = files_.find(path);
    if (it == files_.end()) it = files_.emplace(path, std::make_unique<InputFile>(path)).first;
    return *it->second;
  }

 private:
  std::map<std::string, std::unique_ptr<InputFile>> files_;
};

template <class T>
std::unique_ptr<T> load(FileCache& files, const std::optional<ObjectLocation>& location) {
  if (!location) return nullptr;
  return files.open(location->file).get<T>(location->objectPath);
}

}  // namespace

Workspace Workspace::fromCard(const Datacard& card, const std::string& modelName,
                              const std::string& modelConfig) {
  card.validate();
  Workspace ws;
  ws.card_ = card;
  ws.modelName_ = modelName;
  ws.modelConfig_ = modelConfig;

  FileCache files;
  for (const CardChannel& c : card.channels) {
    WorkspaceChannel channel;
    channel.name = c.name;
    channel.kind = c.kind;
    channel.fraction = c.fraction;
    if (c.delta) channel.delta = *c.delta;
    if (c.ptRange) {
      channel.ptLow = c.ptRange->first;
      channel.ptHigh = c.ptRange->second;
    }
    channel.data = load<TGraphErrors>(files, card.dataLocation(c.name));
    channel.crecoil = load<TGraphErrors>(files, card.auxLocation("crecoil", c.name));
    channel.jesRef = load<TH1D>(files, card.auxLocation("jesRef", c.name));
    channel.fsr = load<TH1D>(files, card.auxLocation("fsr", c.name));
    if (channel.kind == ChannelKind::Multijet && channel.crecoil->GetN() < channel.data->GetN()) {
      throw JecSysError("Channel " + c.name + ": crecoil has fewer points than the data");
    }
    if (!ws.ptBinning_) ws.ptBinning_ = load<TH1D>(files, card.auxLocation("ptBinning", c.name));
    ws.channels_.push_back(std::move(channel));
  }

  for (const CardNuisance& n : card.nuisances) {
    WorkspaceNuisance nuisance;
    nuisance.name = n.name;
    nuisance.type = n.type;
    nuisance.values = n.values;
    nuisance.shapes.resize(card.channels.size());
    if (n.type == CardNuisance::Type::Shape) {
      for (std::size_t i = 0; i != card.channels.size(); ++i) {
        if (!n.actsOn(i)) continue;
        nuisance.shapes[i] = load<TH1D>(files, card.nuisanceLocation(n.name, card.channels[i].name));
      }
    }
    ws.nuisances_.push_back(std::move(nuisance));
  }
  return ws;
}

void Workspace::write(const std::string& path, const std::string& cardPath) const {
  OutputFile out(path);

  // Store the card with canonical locations inside this file, so that reading the
  // workspace needs no other file.
  Datacard stored = card_;
  stored.shapes = {{"data", "*", "workspace.root", "channels/$CHANNEL/data"},
                   {"*", "*", "workspace.root", "nuisances/$NUISANCE/$CHANNEL"}};
  stored.aux.clear();
  for (const WorkspaceChannel& c : channels_) {
    out.write("channels/" + c.name + "/data", *c.data);
    const auto writeAux = [&](const std::string& key, const TObject* object) {
      if (!object) return;
      out.write("channels/" + c.name + "/" + key, *object);
      stored.aux.push_back({key, c.name, "workspace.root", "channels/$CHANNEL/" + key});
    };
    writeAux("crecoil", c.crecoil.get());
    writeAux("jesRef", c.jesRef.get());
    writeAux("fsr", c.fsr.get());
  }
  for (const WorkspaceNuisance& n : nuisances_) {
    for (std::size_t i = 0; i != channels_.size(); ++i) {
      if (n.shapes[i]) out.write("nuisances/" + n.name + "/" + channels_[i].name, *n.shapes[i]);
    }
  }
  if (ptBinning_) out.write("aux/ptBinning", *ptBinning_);
  stored.headerComments = {"Datacard as stored in the workspace (object locations rewritten)."};
  out.writeText("card/text", stored.toText());
  out.writeText("card/original", card_.toText());
  out.writeText("model/name", modelName_);
  out.writeText("model/config", modelConfig_);
  out.writeText("_meta/workspace/card", cardPath);
  out.commit();
}

Workspace Workspace::read(const std::string& path) {
  const InputFile in(path);
  Workspace ws;
  ws.path_ = path;
  ws.card_ = Datacard::parse(in.getText("card/text"), ".");
  ws.modelName_ = in.getText("model/name");
  ws.modelConfig_ = in.getText("model/config");
  ws.ptBinning_ = in.find<TH1D>("aux/ptBinning");

  for (const CardChannel& c : ws.card_.channels) {
    WorkspaceChannel channel;
    channel.name = c.name;
    channel.kind = c.kind;
    channel.fraction = c.fraction;
    if (c.delta) channel.delta = *c.delta;
    if (c.ptRange) {
      channel.ptLow = c.ptRange->first;
      channel.ptHigh = c.ptRange->second;
    }
    const std::string dir = "channels/" + c.name + "/";
    channel.data = in.get<TGraphErrors>(dir + "data");
    channel.crecoil = in.find<TGraphErrors>(dir + "crecoil");
    channel.jesRef = in.find<TH1D>(dir + "jesRef");
    channel.fsr = in.find<TH1D>(dir + "fsr");
    if (channel.kind == ChannelKind::Multijet && !channel.crecoil) {
      throw JecSysError(path + ": multijet channel " + c.name + " has no crecoil");
    }
    ws.channels_.push_back(std::move(channel));
  }
  for (const CardNuisance& n : ws.card_.nuisances) {
    WorkspaceNuisance nuisance;
    nuisance.name = n.name;
    nuisance.type = n.type;
    nuisance.values = n.values;
    nuisance.shapes.resize(ws.card_.channels.size());
    if (n.type == CardNuisance::Type::Shape) {
      for (std::size_t i = 0; i != ws.card_.channels.size(); ++i) {
        if (!n.actsOn(i)) continue;
        nuisance.shapes[i] = in.get<TH1D>("nuisances/" + n.name + "/" + ws.card_.channels[i].name);
      }
    }
    ws.nuisances_.push_back(std::move(nuisance));
  }
  return ws;
}
