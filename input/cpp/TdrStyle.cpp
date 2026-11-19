#include "TdrStyle.h"

#include <map>
#include <tuple>
#include <type_traits>
#include <utility>

#include "ConfigFile.h"
#include "JecSysError.h"
#include "TStyle.h"

namespace {

using Json = nlohmann::json;

// One JSON argument as the parameter type of a setter. An integer is accepted for a
// floating-point parameter; a real number for an integer parameter is an error.
template <class T>
T convert(const Json& value, const std::string& where) {
  if constexpr (std::is_same_v<T, bool>) {
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number_integer()) return value.get<long long>() != 0;
  } else if constexpr (std::is_integral_v<T>) {
    if (value.is_number_integer()) return static_cast<T>(value.get<long long>());
  } else if constexpr (std::is_floating_point_v<T>) {
    if (value.is_number()) return static_cast<T>(value.get<double>());
  } else if constexpr (std::is_same_v<T, const char*>) {
    if (value.is_string()) return value.get_ref<const std::string&>().c_str();
  }
  throw JecSysError(where + ": argument " + value.dump() + " has the wrong type");
}

// Calls a setter with the converted arguments; with style == nullptr only converts them
// (the check at start-up).
using Setter = std::function<void(TStyle* style, const Json& arguments, const std::string& where)>;

template <class C, class... A, std::size_t... I>
void invoke(TStyle* style, void (C::*method)(A...), const Json& arguments, const std::string& where,
            std::index_sequence<I...>) {
  const std::tuple<std::decay_t<A>...> values{convert<std::decay_t<A>>(arguments[I], where)...};
  if (style) std::apply([&](auto... value) { (style->*method)(value...); }, values);
}

template <class C, class... A>
Setter setter(void (C::*method)(A...)) {
  return [method](TStyle* style, const Json& arguments, const std::string& where) {
    if (arguments.size() != sizeof...(A)) {
      throw JecSysError(where + ": expects " + std::to_string(sizeof...(A)) + " argument(s)");
    }
    invoke(style, method, arguments, where, std::index_sequence_for<A...>{});
  };
}

// The axis setters, (value, axis = "X") with the default of TStyle.h.
template <class V>
Setter axisSetter(void (TStyle::*method)(V, Option_t*)) {
  return [method](TStyle* style, const Json& arguments, const std::string& where) {
    if (arguments.size() != 1 && arguments.size() != 2) throw JecSysError(where + ": expects 1 or 2 arguments");
    const V value = convert<V>(arguments[0], where);
    const char* axis = arguments.size() == 2 ? convert<const char*>(arguments[1], where) : "X";
    if (style) (style->*method)(value, axis);
  };
}

#define JECSYS_SETTER(name) {#name, setter(&TStyle::name)}
#define JECSYS_AXIS_SETTER(name) {#name, axisSetter(&TStyle::name)}

// The TStyle setters a configuration may use. Add a line to allow another one.
const std::map<std::string, Setter>& setters() {
  static const std::map<std::string, Setter> table = {
      JECSYS_SETTER(SetCanvasBorderMode), JECSYS_SETTER(SetCanvasColor), JECSYS_SETTER(SetCanvasDefH),
      JECSYS_SETTER(SetCanvasDefW), JECSYS_SETTER(SetCanvasDefX), JECSYS_SETTER(SetCanvasDefY),
      JECSYS_SETTER(SetPadBorderMode), JECSYS_SETTER(SetPadColor), JECSYS_SETTER(SetPadGridX),
      JECSYS_SETTER(SetPadGridY), JECSYS_SETTER(SetGridColor), JECSYS_SETTER(SetGridStyle),
      JECSYS_SETTER(SetGridWidth), JECSYS_SETTER(SetFrameBorderMode), JECSYS_SETTER(SetFrameBorderSize),
      JECSYS_SETTER(SetFrameFillColor), JECSYS_SETTER(SetFrameFillStyle), JECSYS_SETTER(SetFrameLineColor),
      JECSYS_SETTER(SetFrameLineStyle), JECSYS_SETTER(SetFrameLineWidth), JECSYS_SETTER(SetHistLineColor),
      JECSYS_SETTER(SetHistLineStyle), JECSYS_SETTER(SetHistLineWidth), JECSYS_SETTER(SetEndErrorSize),
      JECSYS_SETTER(SetMarkerStyle), JECSYS_SETTER(SetOptFit), JECSYS_SETTER(SetFitFormat),
      JECSYS_SETTER(SetFuncColor), JECSYS_SETTER(SetFuncStyle), JECSYS_SETTER(SetFuncWidth),
      JECSYS_SETTER(SetOptDate), JECSYS_SETTER(SetOptFile),
      {"SetOptStat", setter(static_cast<void (TStyle::*)(Int_t)>(&TStyle::SetOptStat))},
      JECSYS_SETTER(SetStatColor), JECSYS_SETTER(SetStatFont), JECSYS_SETTER(SetStatFontSize),
      JECSYS_SETTER(SetStatTextColor), JECSYS_SETTER(SetStatFormat), JECSYS_SETTER(SetStatBorderSize),
      JECSYS_SETTER(SetStatH), JECSYS_SETTER(SetStatW), JECSYS_SETTER(SetPadTopMargin),
      JECSYS_SETTER(SetPadBottomMargin), JECSYS_SETTER(SetPadLeftMargin), JECSYS_SETTER(SetPadRightMargin),
      JECSYS_SETTER(SetOptTitle), JECSYS_AXIS_SETTER(SetTitleFont), JECSYS_AXIS_SETTER(SetTitleColor),
      JECSYS_SETTER(SetTitleTextColor), JECSYS_SETTER(SetTitleFillColor), JECSYS_SETTER(SetTitleFontSize),
      JECSYS_AXIS_SETTER(SetTitleSize), JECSYS_SETTER(SetTitleXOffset), JECSYS_SETTER(SetTitleYOffset),
      JECSYS_AXIS_SETTER(SetLabelColor), JECSYS_AXIS_SETTER(SetLabelFont), JECSYS_AXIS_SETTER(SetLabelOffset),
      JECSYS_AXIS_SETTER(SetLabelSize), JECSYS_AXIS_SETTER(SetAxisColor), JECSYS_SETTER(SetStripDecimals),
      JECSYS_AXIS_SETTER(SetTickLength), JECSYS_AXIS_SETTER(SetNdivisions), JECSYS_SETTER(SetPadTickX),
      JECSYS_SETTER(SetPadTickY), JECSYS_SETTER(SetOptLogx), JECSYS_SETTER(SetOptLogy), JECSYS_SETTER(SetOptLogz),
      {"SetPaperSize", setter(static_cast<void (TStyle::*)(Float_t, Float_t)>(&TStyle::SetPaperSize))},
      JECSYS_SETTER(SetHatchesLineWidth), JECSYS_SETTER(SetHatchesSpacing),
  };
  return table;
}

#undef JECSYS_SETTER
#undef JECSYS_AXIS_SETTER

}  // namespace

TdrStyle::TdrStyle(const nlohmann::json& settings, const std::string& where) {
  ConfigFile::checkKeys(settings, {"name", "title", "setters"}, where);
  name_ = ConfigFile::get<std::string>(settings, "name", where);
  title_ = ConfigFile::get<std::string>(settings, "title", where);
  const auto list = ConfigFile::get<nlohmann::json>(settings, "setters", where);
  if (!list.is_array()) throw JecSysError(where + ": 'setters' must be a list");
  for (const auto& entry : list) {
    if (!entry.is_array() || entry.empty() || !entry[0].is_string()) {
      throw JecSysError(where + ": a setter must be [\"Name\", arguments...], got " + entry.dump());
    }
    Call call{entry[0].get<std::string>(), nlohmann::json(entry.begin() + 1, entry.end())};
    const std::string context = where + ": " + entry.dump();
    const auto it = setters().find(call.setter);
    if (it == setters().end()) throw JecSysError(context + ": unknown TStyle setter (see TdrStyle.cpp)");
    it->second(nullptr, call.arguments, context);  // checks the arguments
    calls_.push_back(std::move(call));
  }
}

namespace {
// The current style when the first TdrStyle call of the process is made: ROOT's initial
// style ("Modern" by default), which the macros found at the start of their session.
TStyle* sessionStyle() {
  static TStyle* const style = gStyle;
  return style;
}
}  // namespace

void TdrStyle::set() const {
  sessionStyle();  // remembers the initial style before the first change
  auto* style = new TStyle(name_.c_str(), title_.c_str());  // owned by gROOT's list of styles
  for (const Call& call : calls_) setters().at(call.setter)(style, call.arguments, call.setter);
  style->cd();
}

void TdrStyle::setAsFirst() const {
  if (TStyle* initial = sessionStyle()) initial->cd();
  set();
}

void TdrStyle::setAsInChain() const {
  set();
  set();
}
