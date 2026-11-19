#pragma once
// The kinds of channel a datacard can declare.
// This is the single place that knows their names.
#include <string>

enum class ChannelKind {
  Response,    // response graph compared with the JES model
  Multijet,    // as Response, but data is first scaled by the model at the recoil pT
  PfFraction,  // PF energy-fraction difference with a dead zone `delta`
};

ChannelKind parseChannelKind(const std::string& name);  // throws JecSysError if unknown
std::string toString(ChannelKind kind);
