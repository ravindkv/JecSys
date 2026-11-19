#pragma once
// Part 3 of Reprocess: the JEC reference bands and the L1 systematics of every eta bin,
// written to ratio/eta<..>/ (herr*, hrun1, hjes, hl1*, hrc*, hsys*, hruncl). Each pT bin
// averages the corrections over the eta bins of [eta1, eta2] (both signs), weighted with
// the dijet eta distribution at that pT; a correction at a corrected pT comes from the
// JecInverter (getx, memo or secant, see JecInverter.h).
#include <map>
#include <string>

#include "JecInverter.h"
#include "ReprocessSettings.h"

class TDirectory;
class TH1D;
class TH2;

class JecBands {
 public:
  JecBands(const ReprocessSettings& settings, std::string corLevel, std::string epochName, JecInverter::Mode mode);

  // `h2`: the dijet pT-eta weights. Writes into out/ratio/eta<..>/.
  void make(TDirectory& out, TH2& h2);

 private:
  struct Correctors;
  void fillBin(std::map<std::string, TH1D*>& hists, int ipt, double pt, double eta1, double eta2, TH2& h2,
               const Correctors& cor, JecInverter& getJec) const;

  const ReprocessSettings& cfg_;
  std::string corLevel_, epochName_;
  JecInverter::Mode mode_;
};
