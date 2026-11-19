#pragma once
// The samples of Reprocess: the list of input-json "samples" with the flavour sets
// expanded (@zjetFlavor -> zi, zb, ...; @zjetFlavorMc -> zii, zib, ...), the parent,
// tag and generator letters of the flavour samples, and the product table (which tier,
// eta bin, type and alpha exist for each sample).
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ReprocessSettings.h"

class SampleTable {
 public:
  SampleTable(const ReprocessSettings& settings, const std::string& where);

  const std::vector<std::string>& names() const { return names_; }
  // "zjet" for zi, zii, ...; empty for the samples that are not flavour samples.
  std::string parent(const std::string& sample) const;
  char tag(const std::string& sample) const { return tag_.at(sample); }
  // The generator letter of an MC-only flavour sample (zib -> 'b'); 0 for the others.
  char generator(const std::string& sample) const;
  bool isFlavorMc(const std::string& sample) const { return flavorMc_.count(sample) != 0; }
  // True if the product table has (tier, eta bin, type) for the sample.
  bool has(const std::string& sample, const std::string& tier, int etaBin, const std::string& type) const;
  bool hasAlpha(const std::string& sample, double alpha) const;

 private:
  const ReprocessSettings::Product& product(const std::string& sample) const;
  std::vector<std::string> names_;
  std::map<std::string, std::string> parent_, group_;
  std::map<std::string, char> tag_, generator_;
  std::set<std::string> flavorMc_;
  std::map<std::string, ReprocessSettings::Product> products_;
};
