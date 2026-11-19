#pragma once
// A channel is one data set of the fit together with the rule that turns the model
// into a chi2 contribution. The chi2 engine only sees
// this interface; it knows nothing about samples, methods or epochs.
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ChannelKind.h"
#include "PhysicsModel.h"
#include "Workspace.h"

class TRandom;

// What one data point looks like for given parameters (used for output and plots).
struct PointResult {
  double pt = 0;
  double ptError = 0;
  double value = 0;       // data with the nuisance shifts applied (what the model is compared to)
  double sigma = 0;       // its uncertainty
  double prediction = 0;  // model
  bool used = false;      // enters the chi2
  double chi = 0;         // signed pull of the point, 0 if not used
  // Multijet only: the same point expressed as the response at the recoil pT.
  bool hasRecoil = false;
  double recoilPt = 0, recoilValue = 0;
};

class Channel {
 public:
  virtual ~Channel() = default;

  const std::string& name() const { return name_; }
  virtual ChannelKind kind() const = 0;
  int nPoints() const { return static_cast<int>(pt_.size()); }

  // Add the chi2 terms of this channel to `chi2` and the number of points to `nPoints`.
  // `par` are the model parameters, `theta` the nuisance parameters (card order).
  virtual void addChi2(const double* par, const double* theta, double& chi2, int& nPoints) const = 0;
  virtual std::vector<PointResult> evaluate(const double* par, const double* theta) const = 0;

  // Pseudo-data for the given true parameters: one new y value per point.
  virtual std::vector<double> generate(const double* par, const double* theta, TRandom& random) const = 0;
  void setData(const std::vector<double>& y);
  const std::vector<double>& data() const { return y_; }

 protected:
  Channel(const WorkspaceChannel& channel, const PhysicsModel& model, double ptMin);

  // Sum of the nuisance shifts at point i.
  double shift(std::size_t i, const double* theta) const {
    double total = 0;
    for (const auto& [index, size] : shifts_[i]) total += theta[index] * size;
    return total;
  }

  std::string name_;
  const PhysicsModel& model_;
  double ptMin_;
  std::vector<double> pt_, ptError_, y_, sigma_;
  std::vector<double> jesRef_;  // closure reference at pt (1 if the channel has none)
  // Per point: (nuisance index, size of a +1 sigma shift), in nuisance order.
  std::vector<std::vector<std::pair<int, double>>> shifts_;

  friend std::unique_ptr<Channel> makeChannel(const Workspace&, std::size_t, const PhysicsModel&);
};

// Build the channel `index` of the workspace, including its nuisance shifts.
std::unique_ptr<Channel> makeChannel(const Workspace& workspace, std::size_t index,
                                     const PhysicsModel& model);
