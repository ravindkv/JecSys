#include "SampleTable.h"

#include "JecSysError.h"

SampleTable::SampleTable(const ReprocessSettings& settings, const std::string& where) : products_(settings.products) {
  std::map<std::string, std::vector<std::string>> expansions;
  for (const auto& [parent, set] : settings.flavorSets) {
    std::vector<std::string>& single = expansions["@" + parent + "Flavor"];
    std::vector<std::string>& twice = expansions["@" + parent + "FlavorMc"];
    for (const std::string& tag : set.letters) {
      const std::string sample = set.prefix + tag;
      single.push_back(sample);
      parent_[sample] = parent;
      tag_[sample] = tag[0];
      group_[sample] = parent + "Flavor";
    }
    for (const std::string& tag : set.letters) {
      for (const std::string& gen : set.letters) {
        const std::string sample = set.prefix + tag + gen;
        twice.push_back(sample);
        parent_[sample] = parent;
        tag_[sample] = tag[0];
        generator_[sample] = gen[0];
        group_[sample] = parent + "FlavorMc";
        flavorMc_.insert(sample);
      }
    }
  }
  for (const std::string& entry : settings.samples) {
    if (!entry.empty() && entry[0] == '@') {
      const auto it = expansions.find(entry);
      if (it == expansions.end()) throw JecSysError(where + ": unknown sample group '" + entry + "'");
      names_.insert(names_.end(), it->second.begin(), it->second.end());
    } else {
      names_.push_back(entry);
      group_[entry] = entry;
    }
  }
  for (const std::string& sample : names_) {
    if (!products_.count(group_.at(sample))) {
      throw JecSysError(where + ": no 'products' entry for " + group_.at(sample));
    }
  }
}

std::string SampleTable::parent(const std::string& sample) const {
  const auto it = parent_.find(sample);
  return it == parent_.end() ? "" : it->second;
}

char SampleTable::generator(const std::string& sample) const {
  const auto it = generator_.find(sample);
  return it == generator_.end() ? 0 : it->second;
}

const ReprocessSettings::Product& SampleTable::product(const std::string& sample) const {
  return products_.at(group_.at(sample));
}

bool SampleTable::has(const std::string& sample, const std::string& tier, int etaBin, const std::string& type) const {
  const ReprocessSettings::Product& p = product(sample);
  return p.tiers.count(tier) && p.etaBins.count(etaBin) && p.types.count(type);
}

bool SampleTable::hasAlpha(const std::string& sample, double alpha) const {
  return product(sample).alphas.count(alpha) != 0;
}
