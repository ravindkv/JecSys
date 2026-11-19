#include "ShapeFunction.h"

#include <map>

#include "JecSysError.h"
#include "TMath.h"

namespace {

struct FormInfo {
  ShapeFunction::Form form;
  std::size_t nPar;
};

const std::map<std::string, FormInfo>& forms() {
  using Form = ShapeFunction::Form;
  static const std::map<std::string, FormInfo> table = {
      {"const", {Form::Const, 1}},
      {"spr", {Form::Spr, 4}},
      {"trk", {Form::Trk, 4}},
      {"frag", {Form::Frag, 6}},
      {"l1", {Form::L1, 3}},
      {"flavor", {Form::Flavor, 3}},
      {"sigmoidPow", {Form::SigmoidPow, 6}},
      {"sigmoidPow03Inv", {Form::SigmoidPow03Inv, 6}},
      {"sigmoidPowInv", {Form::SigmoidPowInv, 7}},
      {"powInv", {Form::PowInv, 4}},
  };
  return table;
}

// TFormula translates pow() to TMath::Power and log() to TMath::Log; use the same.
inline double power(double x, double y) { return TMath::Power(x, y); }

}  // namespace

ShapeFunction::ShapeFunction(const std::string& form, std::vector<double> par, const std::string& where)
    : p_(std::move(par)) {
  const auto it = forms().find(form);
  if (it == forms().end()) throw JecSysError(where + ": unknown shape form '" + form + "'");
  if (p_.size() != it->second.nPar) {
    throw JecSysError(where + ": form '" + form + "' needs " + std::to_string(it->second.nPar) +
                      " parameters, got " + std::to_string(p_.size()));
  }
  form_ = it->second.form;
}

double ShapeFunction::operator()(double x) const {
  const double* p = p_.data();
  switch (form_) {
    case Form::Const:
      return p[0];
    case Form::Spr:
      return p[0] + p[1] * power(x / p[2], p[3]) / (1 + power(x / p[2], p[3])) *
                        (1 - power(x / p[2], -p[3]));
    case Form::Trk:
      return p[0] + p[1] * power(x / 208., p[2]) + p[3] / x;
    case Form::Frag:
      return p[0] + p[1] * (1 + (power(x / p[2], p[3]) - 1) / (power(x / p[2], p[3]) + 1)) +
             p[4] / x + p[5] * TMath::Log(x) / x;
    case Form::L1:
      return 1 - (p[0] + p[1] * TMath::Log(x) + p[2] * TMath::Power(TMath::Log(x), 2)) / x;
    case Form::Flavor:
      return 1 + 0.01 * (p[0] + p[1] * (power(0.01 * x, p[2]) - 1));
    case Form::SigmoidPow:
      return p[0] + p[1] * (1 + (power(x / p[2], p[3]) - 1) / (power(x / p[2], p[3]) + 1)) +
             p[4] * power(x, p[5]);
    case Form::SigmoidPow03Inv:
      return p[0] + p[1] * (1 + (power(x / p[2], p[3]) - 1) / (power(x / p[2], p[3]) + 1)) +
             p[4] * power(x, +0.3) + p[5] / x;
    case Form::SigmoidPowInv:
      return p[0] + p[1] * (1 + (power(x / p[2], p[3]) - 1) / (power(x / p[2], p[3]) + 1)) +
             p[4] * power(x, p[5]) + p[6] / x;
    case Form::PowInv:
      return p[0] + p[1] * power(x, p[2]) + p[3] / x;
  }
  return 0;
}
