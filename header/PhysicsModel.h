#pragma once
// Physics model of a fit: the parameters of interest and how they predict the jet
// response and the PF energy fractions. A model is chosen
// by name ("jecWorkspace -P JesModel9") and built from its JSON configuration.
#include <memory>
#include <string>
#include <vector>

struct FitParameter {
  std::string name;
  double start = 0;
  double step = 0.01;
  double lower = -100, upper = 100;
  bool penalized = false;  // adds value^2 to the chi2 (unit Gaussian prior)
};

class PhysicsModel {
 public:
  virtual ~PhysicsModel() = default;
  virtual std::string name() const = 0;
  virtual const std::vector<FitParameter>& parameters() const = 0;

  // Jet response at `pt` (before division by a closure reference). Pure function.
  virtual double response(double pt, const double* par) const = 0;

  // Index of a PF fraction ("chf", "nhf", "nef"); throws JecSysError if unknown.
  virtual int fractionIndex(const std::string& fraction) const = 0;
  // Predicted data-MC difference of that PF fraction at `pt`.
  virtual double pfFraction(int fractionIndex, double pt, const double* par) const = 0;
};

// Factory. `configText` is the resolved JSON configuration of the model.
std::unique_ptr<PhysicsModel> makePhysicsModel(const std::string& name, const std::string& configText);
std::vector<std::string> physicsModelNames();
