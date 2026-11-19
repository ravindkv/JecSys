#pragma once
// Part 1 of Reprocess: one graph per (tier, eta bin, type, sample, alpha), written to
// <tier>/eta<..>/<type>_<sample>_a<alpha> (the uncleaned copy to orig/), and the event
// counts that clean them. A graph is read from an input file, merged (zlljet) or built
// from the data and MC graphs (ratio tier); then cleaned, corrected and styled.
//
// ROOT state follows the macro on purpose: the current directory decides where the
// projections of 2D histograms are attached (a later ProfileX of the same name reuses
// the object), and the multijet counts of the ratio tier are the in-memory data counts,
// rebinned a second time.
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "ReprocessSettings.h"
#include "SampleTable.h"

class TDirectory;
class TF1;
class TGraphErrors;
class TH1;
class TObject;

class GraphMaker {
 public:
  GraphMaker(const ReprocessSettings& settings, const SampleTable& samples, std::string corLevel,
             std::string epochName);
  ~GraphMaker();

  // The input file (or directory) of each input key: multijet, incjet, pfjet_data,
  // pfjet_mc, hadw, gamjet, zeejet, zmmjet, zjet.
  void setInputs(std::map<std::string, TDirectory*> inputs) { inputs_ = std::move(inputs); }
  // The TF1s of the mass and recoil corrections (f1zeeMass, f1zmmMass, f1gamMass, f1mjb).
  void makeCorrectionFunctions();
  // All graphs of the output file `out`: <tier>/<eta bin>/ for every tier and eta bin.
  void makeAll(TDirectory& out);

 private:
  using Key = std::tuple<std::string, std::string, std::string, int, int>;  // tier, type, sample, ieta, ialpha
  using CountKey = std::tuple<std::string, std::string, int, int>;          // tier, sample, ieta, ialpha

  void make(const std::string& tier, int ieta, double eta1, double eta2, const std::string& type,
            const std::string& sample, int ialpha, double alpha, TDirectory& dout);
  std::string objectPath(const std::string& tier, double eta1, double eta2, const std::string& type,
                         const std::string& sample, const std::string& parent, double alpha) const;
  TDirectory& inputFor(const std::string& sample, const std::string& tier) const;
  void clean(TGraphErrors& g, const std::string& tier, const std::string& type, const std::string& sample,
             const std::string& parent, int ieta, int ialpha, bool isFlavorMc) const;
  static void ratioErrors(TGraphErrors& g, const TGraphErrors& gd, const TGraphErrors& gm);
  void multijetRatio(TGraphErrors& g, const std::string& type, const TGraphErrors& gd, const TGraphErrors& gm) const;
  void applyCorrections(TGraphErrors& g, const std::string& tier, const std::string& type,
                        const std::string& sample) const;
  void setStyle(TGraphErrors& g, const std::string& tier, const std::string& type, const std::string& sample,
                const std::string& parent, double alpha) const;
  TGraphErrors* graph(const Key& key) const;
  TGraphErrors* adopt(TGraphErrors* graph);

  const ReprocessSettings& cfg_;
  const SampleTable& samples_;
  std::string corLevel_, epochName_;
  std::map<std::string, TDirectory*> inputs_;
  std::map<std::string, std::unique_ptr<TF1>> functions_;
  std::map<Key, TGraphErrors*> graphs_;
  std::map<CountKey, TH1*> counts_;  // attached to their directories
  std::vector<std::unique_ptr<TGraphErrors>> owned_;
};
