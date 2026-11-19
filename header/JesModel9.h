#pragma once
// The 9-parameter JES model of the DP_2021 global fit (was jesFit, njesFit==9):
//   response(pt) = 1 + p0*0.01*ftd + p1*0.01*fp + p2*0.01*fhx + p3*0.01*fhh + p4*0.01*feh
//                    + fragWeight*p5*0.01*fhw + p6*(fl1-1)
//                    + p7*0.01*trkDataMcWeight*(ftd-ftm) + p8*(1-f1q3)
// All numbers are in config/JesModel9.json.
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "PhysicsModel.h"
#include "ShapeFunction.h"

class JesModel9 : public PhysicsModel {
 public:
  static constexpr int kNPar = 9;
  explicit JesModel9(const nlohmann::json& config);

  std::string name() const override { return "JesModel9"; }
  const std::vector<FitParameter>& parameters() const override { return parameters_; }
  double response(double pt, const double* par) const override;
  int fractionIndex(const std::string& fraction) const override;
  double pfFraction(int fractionIndex, double pt, const double* par) const override;

 private:
  std::vector<FitParameter> parameters_;
  double fragWeight_ = 0;
  double trkDataMcWeight_ = 0;
  ShapeFunction ftd_, ftm_, fp_, fhx_, fhh_, feh_, fhw_, fl1_, f1q3_;
  std::vector<std::string> fractionNames_;
  // [fraction][parameter]: shape of the change of the fraction, if any
  std::vector<std::array<std::optional<ShapeFunction>, kNPar>> pfShapes_;
};
