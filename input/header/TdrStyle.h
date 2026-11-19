#pragma once
// The CMS TDR plot style (was setTDRStyle() in tdrstyle_mod15.C), from
// input/config/TdrStyle.json: a list of TStyle setters and their arguments, applied in
// this order. New histograms take their default attributes from the current style, so
// the style is part of what the input creation writes.
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class TStyle;

class TdrStyle {
 public:
  // Settings of input/config/TdrStyle.json (already resolved for an epoch). Every setter
  // name, argument count and argument type is checked here.
  TdrStyle(const nlohmann::json& settings, const std::string& where);

  // One setTDRStyle() call: a new TStyle (it replaces the previous one of the same name,
  // owned by gROOT) with every setter applied, made the current style.
  void set() const;

  // The style of the first setTDRStyle() call in a fresh ROOT session (reprocess, the
  // first step of the chain): the new TStyle takes its axis defaults from the style that
  // is current then, ROOT's initial style. Also right when an earlier epoch of the same
  // process has made other styles current.
  void setAsFirst() const;

  // The style the macro chain has from its second setTDRStyle() call on. In one ROOT
  // session reprocess and softrad3 call setTDRStyle() three times. From the second call
  // on, the TStyle constructor deletes the previous "tdrStyle" (the current gStyle), so
  // TStyle::Reset copies the axis defaults from ROOT's last predefined style. A step that
  // runs after reprocess therefore needs two calls; more calls change nothing.
  void setAsInChain() const;

 private:
  struct Call {
    std::string setter;
    nlohmann::json arguments;
  };
  std::string name_, title_;
  std::vector<Call> calls_;
};
