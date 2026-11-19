#pragma once
// The datacard: a text file that lists the channels of a fit, the nuisance parameters
// and which nuisance acts on which channel, plus where the graphs and histograms are
// stored. It is the contract between the "analysis" side (jecMakeCard) and the
// "statistics" side (jecWorkspace, jecFit). Format: doc/datacard.md.
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ChannelKind.h"

struct CardChannel {
  std::string name;  // "<method>_<sample>", e.g. "ptchs_gamjet"
  ChannelKind kind = ChannelKind::Response;
  std::string fraction;                              // PfFraction only: "chf", "nhf", "nef"
  std::optional<double> delta;                       // PfFraction only, in %
  std::optional<std::pair<double, double>> ptRange;  // PfFraction only, open interval
};

struct CardNuisance {
  enum class Type { Shape, Flat };
  std::string name;
  Type type = Type::Shape;
  // One entry per channel, in channel order. Empty optional = does not act on the
  // channel ("-"). Flat: the shift itself. Shape: a factor on the histogram (normally 1).
  std::vector<std::optional<double>> values;
  std::string comment;  // written after the row, e.g. the source of a number

  bool actsOn(std::size_t channelIndex) const { return values[channelIndex].has_value(); }
  bool isInactive() const;  // acts on no channel
};

// "shapes" and "aux" lines. `key` is "data", a nuisance-name pattern (shapes) or the
// name of an auxiliary input such as "crecoil" (aux). '*' is a wild card.
struct CardRule {
  std::string key;
  std::string channelPattern;
  std::string file;        // as written in the card (relative to the card's directory)
  std::string objectPath;  // may contain $CHANNEL, $METHOD, $SAMPLE, $NUISANCE
};

struct CardGroup {
  std::string name;
  std::vector<std::string> nuisances;
};

// A resolved location of an object: absolute or cwd-relative file plus object path.
struct ObjectLocation {
  std::string file;
  std::string objectPath;
};

class Datacard {
 public:
  // --- content (public data: a card is a plain description, validated by validate()) ---
  std::vector<std::string> headerComments;
  std::string epoch;
  std::string corLevel;
  std::string etaBin;
  double ptMin = 0;
  std::vector<CardRule> shapes;
  std::vector<CardRule> aux;
  std::vector<CardChannel> channels;
  std::vector<CardNuisance> nuisances;
  std::vector<CardGroup> groups;

  // --- input / output ---
  static Datacard read(const std::string& path);  // parses and validates
  static Datacard parse(const std::string& text, const std::string& baseDir);
  std::string toText() const;
  void write(const std::string& path) const;  // validates first

  // Directory that relative file names in `shapes` / `aux` lines refer to.
  const std::string& baseDir() const { return baseDir_; }
  void setBaseDir(const std::string& dir) { baseDir_ = dir; }

  // --- checks ---
  void validate() const;                      // throws JecSysError on the first error
  std::vector<std::string> warnings() const;  // inactive and identical nuisances

  // --- look-up ---
  int channelIndex(const std::string& name) const;   // -1 if unknown
  int nuisanceIndex(const std::string& name) const;  // -1 if unknown
  // The last matching line wins, so general lines go first and exceptions after them.
  std::optional<ObjectLocation> dataLocation(const std::string& channel) const;
  std::optional<ObjectLocation> nuisanceLocation(const std::string& nuisance,
                                                 const std::string& channel) const;
  std::optional<ObjectLocation> auxLocation(const std::string& key,
                                            const std::string& channel) const;
  std::vector<std::string> auxKeys() const;  // distinct keys, in order of appearance

  // Combine several cards into one fit (jecCombineCards). Channels get the prefix
  // "<label>_"; nuisances with the same name become one (correlated) nuisance.
  // File names in the result are relative to `outDir`.
  // A nuisance whose name matches the regular expression `decorrelate` is renamed to
  // "<name>_<label>" first, so that it stays a separate parameter for each card.
  static Datacard combine(const std::vector<std::pair<std::string, Datacard>>& labelledCards,
                          const std::string& outDir, const std::string& decorrelate = "");

  // "<method>_<sample>" -> method / sample (split at the first '_').
  static std::string methodOf(const std::string& channel);
  static std::string sampleOf(const std::string& channel);

 private:
  std::optional<ObjectLocation> resolve(const CardRule& rule, const std::string& channel,
                                        const std::string& nuisance) const;
  std::string baseDir_ = ".";
};
