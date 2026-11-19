#include "Datacard.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "JecSysError.h"
#include "TextTools.h"

using textTools::formatDouble;
using textTools::globMatch;
using textTools::toDouble;

// ---------------------------------------------------------------------------------------
// ChannelKind
// ---------------------------------------------------------------------------------------
ChannelKind parseChannelKind(const std::string& name) {
  if (name == "response") return ChannelKind::Response;
  if (name == "multijet") return ChannelKind::Multijet;
  if (name == "pfFraction") return ChannelKind::PfFraction;
  throw JecSysError("Unknown channel kind '" + name +
                    "' (known: response, multijet, pfFraction)");
}

std::string toString(ChannelKind kind) {
  switch (kind) {
    case ChannelKind::Response: return "response";
    case ChannelKind::Multijet: return "multijet";
    case ChannelKind::PfFraction: return "pfFraction";
  }
  return "?";
}

bool CardNuisance::isInactive() const {
  return std::none_of(values.begin(), values.end(), [](const auto& v) { return v.has_value(); });
}

namespace {

std::string typeName(CardNuisance::Type type) {
  return type == CardNuisance::Type::Shape ? "shape" : "flat";
}

std::string stripComment(const std::string& line, std::string* comment = nullptr) {
  const std::size_t hash = line.find('#');
  if (hash == std::string::npos) return line;
  if (comment) {
    std::size_t start = line.find_first_not_of(" \t", hash + 1);
    *comment = start == std::string::npos ? "" : line.substr(start);
  }
  return line.substr(0, hash);
}

JecSysError cardError(int lineNumber, const std::string& message) {
  return JecSysError("Datacard line " + std::to_string(lineNumber) + ": " + message);
}

// Pad `text` to `width` characters.
std::string pad(const std::string& text, std::size_t width) {
  return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

std::string valueText(const std::optional<double>& value) {
  return value ? formatDouble(*value) : "-";
}

}  // namespace

// ---------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------
Datacard Datacard::read(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw JecSysError("Cannot open datacard '" + path + "'");
  std::stringstream buffer;
  buffer << in.rdbuf();
  try {
    Datacard card = parse(buffer.str(), textTools::dirName(path));
    card.validate();
    return card;
  } catch (const JecSysError& error) {
    throw JecSysError(path + ": " + error.what());
  }
}

Datacard Datacard::parse(const std::string& text, const std::string& baseDir) {
  Datacard card;
  card.baseDir_ = baseDir;

  std::istringstream in(text);
  std::string rawLine;
  int lineNumber = 0;
  bool inHeader = true;
  std::set<std::string> seenKeys;

  // A row that has one column per channel ("kind", "delta", "ptRange", nuisances).
  const auto columns = [&](const std::vector<std::string>& words, std::size_t first) {
    if (card.channels.empty()) throw cardError(lineNumber, "'" + words[0] + "' before 'channel'");
    if (words.size() - first != card.channels.size()) {
      throw cardError(lineNumber, "'" + words[0] + "' has " + std::to_string(words.size() - first) +
                                      " columns, but there are " +
                                      std::to_string(card.channels.size()) + " channels");
    }
    return std::vector<std::string>(words.begin() + first, words.end());
  };
  const auto once = [&](const std::string& key) {
    if (!seenKeys.insert(key).second) throw cardError(lineNumber, "'" + key + "' given twice");
  };

  while (std::getline(in, rawLine)) {
    ++lineNumber;
    std::string comment;
    const std::string line = stripComment(rawLine, &comment);
    const std::vector<std::string> words = textTools::splitWords(line);
    if (words.empty()) {
      if (inHeader && rawLine.find('#') != std::string::npos) card.headerComments.push_back(comment);
      continue;
    }
    inHeader = false;
    const std::string& key = words[0];
    if (key.rfind("---", 0) == 0) continue;  // separator line

    if (key == "epoch" || key == "corLevel" || key == "etaBin" || key == "ptMin") {
      once(key);
      if (words.size() != 2) throw cardError(lineNumber, "expected '" + key + " <value>'");
      if (key == "epoch") card.epoch = words[1];
      if (key == "corLevel") card.corLevel = words[1];
      if (key == "etaBin") card.etaBin = words[1];
      if (key == "ptMin") card.ptMin = toDouble(words[1], "ptMin");
    } else if (key == "shapes" || key == "aux") {
      if (words.size() != 5) {
        throw cardError(lineNumber, "expected '" + key + " <key> <channel> <file> <object path>'");
      }
      (key == "shapes" ? card.shapes : card.aux).push_back({words[1], words[2], words[3], words[4]});
    } else if (key == "channel") {
      once(key);
      if (words.size() < 2) throw cardError(lineNumber, "'channel' needs at least one name");
      for (std::size_t i = 1; i != words.size(); ++i) {
        CardChannel channel;
        channel.name = words[i];
        card.channels.push_back(channel);
      }
    } else if (key == "kind") {
      once(key);
      const auto cells = columns(words, 1);
      try {
        for (std::size_t i = 0; i != cells.size(); ++i) card.channels[i].kind = parseChannelKind(cells[i]);
      } catch (const JecSysError& error) {
        throw cardError(lineNumber, error.what());
      }
    } else if (key == "fraction") {
      once(key);
      const auto cells = columns(words, 1);
      for (std::size_t i = 0; i != cells.size(); ++i) {
        if (cells[i] != "-") card.channels[i].fraction = cells[i];
      }
    } else if (key == "delta") {
      once(key);
      const auto cells = columns(words, 1);
      for (std::size_t i = 0; i != cells.size(); ++i) {
        if (cells[i] != "-") card.channels[i].delta = toDouble(cells[i], "delta");
      }
    } else if (key == "ptRange") {
      once(key);
      const auto cells = columns(words, 1);
      for (std::size_t i = 0; i != cells.size(); ++i) {
        if (cells[i] == "-") continue;
        const auto limits = textTools::split(cells[i], ':');
        if (limits.size() != 2) throw cardError(lineNumber, "ptRange must be '<min>:<max>' or '-'");
        card.channels[i].ptRange = {toDouble(limits[0], "ptRange"), toDouble(limits[1], "ptRange")};
      }
    } else if (key == "group") {
      if (words.size() < 3 || words[2] != "=") {
        throw cardError(lineNumber, "expected 'group <name> = <nuisance> ...'");
      }
      card.groups.push_back({words[1], std::vector<std::string>(words.begin() + 3, words.end())});
    } else {
      // Nuisance row: <name> shape|flat <one value per channel>
      if (words.size() < 2 || (words[1] != "shape" && words[1] != "flat")) {
        throw cardError(lineNumber, "unknown keyword '" + key +
                                        "' (a nuisance row is '<name> shape|flat <values>')");
      }
      CardNuisance nuisance;
      nuisance.name = key;
      nuisance.type = words[1] == "shape" ? CardNuisance::Type::Shape : CardNuisance::Type::Flat;
      nuisance.comment = comment;
      for (const std::string& cell : columns(words, 2)) {
        if (cell == "-") {
          nuisance.values.emplace_back();
        } else {
          try {
            nuisance.values.emplace_back(toDouble(cell, "nuisance '" + key + "'"));
          } catch (const JecSysError& error) {
            throw cardError(lineNumber, error.what());
          }
        }
      }
      card.nuisances.push_back(nuisance);
    }
  }
  return card;
}

// ---------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------
std::string Datacard::toText() const {
  std::ostringstream out;
  for (const std::string& comment : headerComments) out << "# " << comment << "\n";
  const std::string rule(100, '-');
  out << "epoch     " << epoch << "\n";
  out << "corLevel  " << corLevel << "\n";
  out << "etaBin    " << etaBin << "\n";
  out << "ptMin     " << formatDouble(ptMin) << "\n";
  out << rule << "\n";

  // shapes and aux lines, aligned
  const auto writeRules = [&out](const std::string& keyword, const std::vector<CardRule>& rules,
                                 const std::array<std::size_t, 3>& widths) {
    for (const CardRule& r : rules) {
      out << pad(keyword, 7) << " " << pad(r.key, widths[0]) << " " << pad(r.channelPattern, widths[1])
          << " " << pad(r.file, widths[2]) << " " << r.objectPath << "\n";
    }
  };
  std::array<std::size_t, 3> widths{0, 0, 0};
  for (const auto* rules : {&shapes, &aux}) {
    for (const CardRule& r : *rules) {
      widths[0] = std::max(widths[0], r.key.size());
      widths[1] = std::max(widths[1], r.channelPattern.size());
      widths[2] = std::max(widths[2], r.file.size());
    }
  }
  writeRules("shapes", shapes, widths);
  writeRules("aux", aux, widths);
  out << rule << "\n";

  // The channel table. The first two columns are the row name and the nuisance type.
  std::size_t nameWidth = std::string("channel").size();
  for (const CardNuisance& n : nuisances) nameWidth = std::max(nameWidth, n.name.size());
  std::vector<std::size_t> colWidth(channels.size());
  for (std::size_t i = 0; i != channels.size(); ++i) {
    colWidth[i] = std::max(channels[i].name.size(), toString(channels[i].kind).size());
  }
  const auto row = [&](const std::string& name, const std::string& type,
                       const std::vector<std::string>& cells, const std::string& comment) {
    std::string line = pad(name, nameWidth) + " " + pad(type, 5);
    for (std::size_t i = 0; i != cells.size(); ++i) line += " " + pad(cells[i], colWidth[i]);
    if (!comment.empty()) line += " # " + comment;
    line.erase(line.find_last_not_of(' ') + 1);
    out << line << "\n";
  };

  std::vector<std::string> names, kinds, fractions, deltas, ranges;
  bool anyFraction = false, anyDelta = false, anyRange = false;
  for (const CardChannel& c : channels) {
    names.push_back(c.name);
    kinds.push_back(toString(c.kind));
    fractions.push_back(c.fraction.empty() ? "-" : c.fraction);
    anyFraction = anyFraction || !c.fraction.empty();
    deltas.push_back(valueText(c.delta));
    ranges.push_back(c.ptRange ? formatDouble(c.ptRange->first) + ":" + formatDouble(c.ptRange->second)
                               : "-");
    anyDelta = anyDelta || c.delta.has_value();
    anyRange = anyRange || c.ptRange.has_value();
  }
  row("channel", "", names, "");
  row("kind", "", kinds, "");
  if (anyFraction) row("fraction", "", fractions, "");
  if (anyDelta) row("delta", "", deltas, "");
  if (anyRange) row("ptRange", "", ranges, "");
  out << rule << "\n";
  for (const CardNuisance& n : nuisances) {
    std::vector<std::string> cells;
    for (const auto& value : n.values) cells.push_back(valueText(value));
    row(n.name, typeName(n.type), cells, n.comment);
  }
  if (!groups.empty()) out << rule << "\n";
  for (const CardGroup& g : groups) {
    out << "group " << g.name << " = " << textTools::join(g.nuisances, " ") << "\n";
  }
  return out.str();
}

void Datacard::write(const std::string& path) const {
  validate();
  textTools::makeDirs(textTools::dirName(path));
  const std::string tmpPath = path + ".tmp";
  {
    std::ofstream out(tmpPath);
    if (!out) throw JecSysError("Cannot write datacard '" + tmpPath + "'");
    out << toText();
  }
  if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
    throw JecSysError("Cannot rename " + tmpPath + " to " + path);
  }
}

// ---------------------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------------------
void Datacard::validate() const {
  if (epoch.empty()) throw JecSysError("Datacard: 'epoch' is missing");
  if (corLevel.empty()) throw JecSysError("Datacard: 'corLevel' is missing");
  if (etaBin.empty()) throw JecSysError("Datacard: 'etaBin' is missing");
  if (channels.empty()) throw JecSysError("Datacard: no channels");

  std::set<std::string> channelNames;
  for (const CardChannel& c : channels) {
    if (!channelNames.insert(c.name).second) {
      throw JecSysError("Datacard: channel '" + c.name + "' is listed twice");
    }
    if (!dataLocation(c.name)) {
      throw JecSysError("Datacard: no 'shapes data' line matches channel '" + c.name + "'");
    }
    const bool isPf = c.kind == ChannelKind::PfFraction;
    if (isPf && (c.fraction.empty() || !c.delta || !c.ptRange)) {
      throw JecSysError("Datacard: pfFraction channel '" + c.name +
                        "' needs 'fraction', 'delta' and 'ptRange'");
    }
    if (!isPf && (!c.fraction.empty() || c.delta || c.ptRange)) {
      throw JecSysError("Datacard: 'fraction' / 'delta' / 'ptRange' given for channel '" + c.name +
                        "', which is not a pfFraction channel");
    }
    if (c.kind == ChannelKind::Multijet && !auxLocation("crecoil", c.name)) {
      throw JecSysError("Datacard: multijet channel '" + c.name + "' needs an 'aux crecoil' line");
    }
  }

  std::set<std::string> nuisanceNames;
  for (const CardNuisance& n : nuisances) {
    if (!nuisanceNames.insert(n.name).second) {
      throw JecSysError("Datacard: nuisance '" + n.name + "' is listed twice");
    }
    if (channelNames.count(n.name)) {
      throw JecSysError("Datacard: '" + n.name + "' is both a channel and a nuisance");
    }
    if (n.values.size() != channels.size()) {
      throw JecSysError("Datacard: nuisance '" + n.name + "' does not have one value per channel");
    }
    for (std::size_t i = 0; i != channels.size(); ++i) {
      if (!n.actsOn(i)) continue;
      if (channels[i].kind == ChannelKind::PfFraction) {
        throw JecSysError("Datacard: nuisance '" + n.name + "' acts on pfFraction channel '" +
                          channels[i].name + "'; this is not supported");
      }
      if (n.type == CardNuisance::Type::Shape && !nuisanceLocation(n.name, channels[i].name)) {
        throw JecSysError("Datacard: no 'shapes' line matches nuisance '" + n.name +
                          "' in channel '" + channels[i].name + "'");
      }
    }
  }

  std::set<std::string> groupNames;
  for (const CardGroup& g : groups) {
    if (!groupNames.insert(g.name).second) {
      throw JecSysError("Datacard: group '" + g.name + "' is defined twice");
    }
    for (const std::string& name : g.nuisances) {
      if (!nuisanceNames.count(name)) {
        throw JecSysError("Datacard: group '" + g.name + "' names unknown nuisance '" + name + "'");
      }
    }
  }
}

std::vector<std::string> Datacard::warnings() const {
  std::vector<std::string> result;
  // Signature of what a nuisance does: type + per channel the value and, for shapes,
  // the object it reads. Two nuisances with the same signature cannot be separated.
  std::map<std::string, std::string> firstWithSignature;
  for (const CardNuisance& n : nuisances) {
    if (n.isInactive()) {
      result.push_back("nuisance '" + n.name + "' acts on no channel (it stays at 0 +- 1)");
      continue;
    }
    std::string signature = typeName(n.type);
    for (std::size_t i = 0; i != channels.size(); ++i) {
      signature += "|" + valueText(n.values[i]);
      if (n.actsOn(i) && n.type == CardNuisance::Type::Shape) {
        const auto location = nuisanceLocation(n.name, channels[i].name);
        if (location) signature += "@" + location->file + ":" + location->objectPath;
      }
    }
    const auto [it, isNew] = firstWithSignature.emplace(signature, n.name);
    if (!isNew) {
      result.push_back("nuisances '" + it->second + "' and '" + n.name +
                       "' are identical; the fit cannot separate them");
    }
  }
  return result;
}

// ---------------------------------------------------------------------------------------
// Look-up
// ---------------------------------------------------------------------------------------
int Datacard::channelIndex(const std::string& name) const {
  for (std::size_t i = 0; i != channels.size(); ++i) {
    if (channels[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

int Datacard::nuisanceIndex(const std::string& name) const {
  for (std::size_t i = 0; i != nuisances.size(); ++i) {
    if (nuisances[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

std::string Datacard::methodOf(const std::string& channel) {
  return channel.substr(0, channel.find('_'));
}

std::string Datacard::sampleOf(const std::string& channel) {
  const std::size_t underscore = channel.find('_');
  return underscore == std::string::npos ? "" : channel.substr(underscore + 1);
}

std::optional<ObjectLocation> Datacard::resolve(const CardRule& rule, const std::string& channel,
                                                const std::string& nuisance) const {
  std::string path = rule.objectPath;
  path = textTools::replaceAll(path, "$CHANNEL", channel);
  path = textTools::replaceAll(path, "$METHOD", methodOf(channel));
  path = textTools::replaceAll(path, "$SAMPLE", sampleOf(channel));
  path = textTools::replaceAll(path, "$NUISANCE", nuisance);
  return ObjectLocation{textTools::resolvePath(baseDir_, rule.file), path};
}

std::optional<ObjectLocation> Datacard::dataLocation(const std::string& channel) const {
  for (auto rule = shapes.rbegin(); rule != shapes.rend(); ++rule) {
    if (rule->key == "data" && globMatch(rule->channelPattern, channel)) {
      return resolve(*rule, channel, "");
    }
  }
  return std::nullopt;
}

std::optional<ObjectLocation> Datacard::nuisanceLocation(const std::string& nuisance,
                                                         const std::string& channel) const {
  for (auto rule = shapes.rbegin(); rule != shapes.rend(); ++rule) {
    if (rule->key != "data" && globMatch(rule->key, nuisance) &&
        globMatch(rule->channelPattern, channel)) {
      return resolve(*rule, channel, nuisance);
    }
  }
  return std::nullopt;
}

std::optional<ObjectLocation> Datacard::auxLocation(const std::string& key,
                                                    const std::string& channel) const {
  for (auto rule = aux.rbegin(); rule != aux.rend(); ++rule) {
    if (rule->key == key && globMatch(rule->channelPattern, channel)) {
      return resolve(*rule, channel, "");
    }
  }
  return std::nullopt;
}

std::vector<std::string> Datacard::auxKeys() const {
  std::vector<std::string> keys;
  for (const CardRule& rule : aux) {
    if (std::find(keys.begin(), keys.end(), rule.key) == keys.end()) keys.push_back(rule.key);
  }
  return keys;
}

// ---------------------------------------------------------------------------------------
// Combination
// ---------------------------------------------------------------------------------------
Datacard Datacard::combine(const std::vector<std::pair<std::string, Datacard>>& labelledCards,
                           const std::string& outDir, const std::string& decorrelate) {
  if (labelledCards.empty()) throw JecSysError("jecCombineCards: no cards given");
  Datacard out;
  out.baseDir_ = outDir;

  std::vector<std::string> labels, epochs;
  std::set<std::string> corLevels, etaBins;
  for (const auto& [label, card] : labelledCards) {
    if (std::find(labels.begin(), labels.end(), label) != labels.end()) {
      throw JecSysError("jecCombineCards: label '" + label + "' used twice");
    }
    labels.push_back(label);
    epochs.push_back(card.epoch);
    corLevels.insert(card.corLevel);
    etaBins.insert(card.etaBin);
    if (card.ptMin != labelledCards.front().second.ptMin) {
      throw JecSysError("jecCombineCards: the cards have different ptMin");
    }
  }
  out.headerComments.push_back("Combination of: " + textTools::join(labels, ", ") +
                               " (made by jecCombineCards, do not edit by hand)");
  out.epoch = textTools::join(epochs, "+");
  out.corLevel = corLevels.size() == 1 ? *corLevels.begin() : "mixed";
  out.etaBin = etaBins.size() == 1 ? *etaBins.begin() : "mixed";
  out.ptMin = labelledCards.front().second.ptMin;

  const auto isDecorrelated = [&decorrelate](const std::string& name) {
    return !decorrelate.empty() && textTools::regexMatch(decorrelate, name);
  };
  const auto newName = [&](const std::string& name, const std::string& label) {
    return isDecorrelated(name) ? name + "_" + label : name;
  };

  // Channels, with their shapes and aux lines written out one channel at a time so
  // that $CHANNEL etc. keep referring to the original (unprefixed) names.
  for (const auto& [label, card] : labelledCards) {
    const auto relocate = [&](const ObjectLocation& location, const std::string& key,
                              const std::string& newChannel, std::string objectPath) {
      return CardRule{key, newChannel, textTools::relativePath(location.file, outDir),
                      std::move(objectPath)};
    };
    for (const CardChannel& channel : card.channels) {
      CardChannel renamed = channel;
      renamed.name = label + "_" + channel.name;
      out.channels.push_back(renamed);

      for (const CardRule& rule : card.shapes) {
        if (!globMatch(rule.channelPattern, channel.name)) continue;
        // Keep $NUISANCE as a template; fix the channel-dependent parts now.
        const auto location = card.resolve(rule, channel.name, "$NUISANCE");
        if (rule.key == "data") {
          out.shapes.push_back(relocate(*location, rule.key, renamed.name, location->objectPath));
          continue;
        }
        // A decorrelated nuisance has a new name but reads the original histogram, so
        // it needs its own line with the original name written out.
        if (!isDecorrelated(rule.key)) {
          out.shapes.push_back(relocate(*location, rule.key, renamed.name, location->objectPath));
        }
        const int channelIndex = card.channelIndex(channel.name);
        for (const CardNuisance& n : card.nuisances) {
          if (!isDecorrelated(n.name) || !n.actsOn(channelIndex) || !globMatch(rule.key, n.name)) continue;
          const auto fixed = card.resolve(rule, channel.name, n.name);
          out.shapes.push_back(relocate(*fixed, n.name + "_" + label, renamed.name, fixed->objectPath));
        }
      }
      for (const CardRule& rule : card.aux) {
        if (!globMatch(rule.channelPattern, channel.name)) continue;
        const auto location = card.resolve(rule, channel.name, "");
        out.aux.push_back(relocate(*location, rule.key, renamed.name, location->objectPath));
      }
    }
  }

  // Nuisances: same name = same parameter. Order: first appearance.
  std::size_t offset = 0;
  for (const auto& [label, card] : labelledCards) {
    for (const CardNuisance& n : card.nuisances) {
      const std::string name = newName(n.name, label);
      int index = out.nuisanceIndex(name);
      if (index < 0) {
        CardNuisance merged;
        merged.name = name;
        merged.type = n.type;
        merged.comment = n.comment;
        merged.values.resize(out.channels.size());
        out.nuisances.push_back(merged);
        index = static_cast<int>(out.nuisances.size()) - 1;
      } else if (out.nuisances[index].type != n.type) {
        throw JecSysError("jecCombineCards: nuisance '" + n.name +
                          "' is 'shape' in one card and 'flat' in another");
      }
      for (std::size_t i = 0; i != card.channels.size(); ++i) {
        out.nuisances[index].values[offset + i] = n.values[i];
      }
    }
    offset += card.channels.size();
  }

  // Groups: union of the members, in order of appearance.
  for (const auto& [label, card] : labelledCards) {
    for (const CardGroup& original : card.groups) {
      CardGroup g{original.name, {}};
      for (const std::string& name : original.nuisances) g.nuisances.push_back(newName(name, label));
      auto existing = std::find_if(out.groups.begin(), out.groups.end(),
                                   [&](const CardGroup& o) { return o.name == g.name; });
      if (existing == out.groups.end()) {
        out.groups.push_back(g);
        continue;
      }
      for (const std::string& name : g.nuisances) {
        if (std::find(existing->nuisances.begin(), existing->nuisances.end(), name) ==
            existing->nuisances.end()) {
          existing->nuisances.push_back(name);
        }
      }
    }
  }
  out.validate();
  return out;
}
