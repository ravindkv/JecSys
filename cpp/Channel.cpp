#include "Channel.h"

#include <algorithm>
#include <cmath>

#include "JecSysError.h"
#include "TRandom.h"

Channel::Channel(const WorkspaceChannel& channel, const PhysicsModel& model, double ptMin)
    : name_(channel.name), model_(model), ptMin_(ptMin) {
  const TGraphErrors& graph = *channel.data;
  const int n = graph.GetN();
  pt_.assign(graph.GetX(), graph.GetX() + n);
  ptError_.assign(graph.GetEX(), graph.GetEX() + n);
  y_.assign(graph.GetY(), graph.GetY() + n);
  sigma_.assign(graph.GetEY(), graph.GetEY() + n);
  jesRef_.assign(n, 1.);
  if (channel.jesRef) {
    for (int i = 0; i != n; ++i) jesRef_[i] = channel.jesRef->Interpolate(pt_[i]);
  }
  shifts_.resize(n);
}

void Channel::setData(const std::vector<double>& y) {
  if (y.size() != y_.size()) throw JecSysError("Channel " + name_ + ": wrong number of toy points");
  y_ = y;
}

namespace {

// ---------------------------------------------------------------------------------------
// Response: (data + shifts - model) / sigma
// ---------------------------------------------------------------------------------------
class ResponseChannel : public Channel {
 public:
  ResponseChannel(const WorkspaceChannel& channel, const PhysicsModel& model, double ptMin)
      : Channel(channel, model, ptMin) {}
  ChannelKind kind() const override { return ChannelKind::Response; }

  void addChi2(const double* par, const double* theta, double& chi2, int& nPoints) const override {
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      if (pt_[i] < ptMin_) continue;
      const double fit = model_.response(pt_[i], par) / jesRef_[i];
      const double chi = (y_[i] + shift(i, theta) - fit) / sigma_[i];
      chi2 += chi * chi;
      ++nPoints;
    }
  }

  std::vector<PointResult> evaluate(const double* par, const double* theta) const override {
    std::vector<PointResult> points(pt_.size());
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      PointResult& p = points[i];
      p.pt = pt_[i];
      p.ptError = ptError_[i];
      p.prediction = model_.response(pt_[i], par) / jesRef_[i];
      p.value = y_[i] + shift(i, theta);
      p.sigma = sigma_[i];
      p.used = pt_[i] >= ptMin_;
      if (p.used) p.chi = (p.value - p.prediction) / p.sigma;
    }
    return points;
  }

  std::vector<double> generate(const double* par, const double* theta, TRandom& random) const override {
    std::vector<double> y(pt_.size());
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      const double fit = model_.response(pt_[i], par) / jesRef_[i];
      y[i] = fit - shift(i, theta) + random.Gaus(0, sigma_[i]);
    }
    return y;
  }
};

// ---------------------------------------------------------------------------------------
// Multijet: the measured ratio is response(pt) / response(recoil pt), so data and sigma
// are first multiplied by the model at the recoil pT (was globalFitL3Res.C:3041).
// ---------------------------------------------------------------------------------------
class MultijetChannel : public Channel {
 public:
  MultijetChannel(const WorkspaceChannel& channel, const PhysicsModel& model, double ptMin)
      : Channel(channel, model, ptMin) {
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      const double recoilPt = channel.crecoil->GetY()[i] * pt_[i];
      recoilPt_.push_back(recoilPt);
      recoilJesRef_.push_back(channel.jesRef ? channel.jesRef->Interpolate(recoilPt) : 1.);
    }
  }
  ChannelKind kind() const override { return ChannelKind::Multijet; }

  void addChi2(const double* par, const double* theta, double& chi2, int& nPoints) const override {
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      if (pt_[i] < ptMin_) continue;
      const double fit = model_.response(pt_[i], par) / jesRef_[i];
      const double fitRef = model_.response(recoilPt_[i], par) / recoilJesRef_[i];
      const double data = y_[i] * fitRef;
      const double sigma = sigma_[i] * fitRef;
      const double chi = (data + shift(i, theta) - fit) / sigma;
      chi2 += chi * chi;
      ++nPoints;
    }
  }

  std::vector<PointResult> evaluate(const double* par, const double* theta) const override {
    std::vector<PointResult> points(pt_.size());
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      PointResult& p = points[i];
      const double fitRef = model_.response(recoilPt_[i], par) / recoilJesRef_[i];
      p.pt = pt_[i];
      p.ptError = ptError_[i];
      p.prediction = model_.response(pt_[i], par) / jesRef_[i];
      p.value = y_[i] * fitRef + shift(i, theta);
      p.sigma = sigma_[i] * fitRef;
      p.used = pt_[i] >= ptMin_;
      if (p.used) p.chi = (p.value - p.prediction) / p.sigma;
      // MJB = jes / jesRecoil  =>  "jesRecoil" = jes * jesRecoil / shifted data
      p.hasRecoil = true;
      p.recoilPt = recoilPt_[i];
      p.recoilValue = p.prediction * fitRef / p.value;
    }
    return points;
  }

  std::vector<double> generate(const double* par, const double* theta, TRandom& random) const override {
    std::vector<double> y(pt_.size());
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      const double fit = model_.response(pt_[i], par) / jesRef_[i];
      const double fitRef = model_.response(recoilPt_[i], par) / recoilJesRef_[i];
      y[i] = (fit - shift(i, theta)) / fitRef + random.Gaus(0, sigma_[i]);
    }
    return y;
  }

 private:
  std::vector<double> recoilPt_, recoilJesRef_;
};

// ---------------------------------------------------------------------------------------
// PF fraction: max(|data - model| - delta, 0) / sigma inside an open pT interval
// (was globalFitL3Res.C:3106-3262, four copies).
// ---------------------------------------------------------------------------------------
class PfFractionChannel : public Channel {
 public:
  PfFractionChannel(const WorkspaceChannel& channel, const PhysicsModel& model, double ptMin)
      : Channel(channel, model, ptMin),
        fraction_(model.fractionIndex(channel.fraction)),
        delta_(channel.delta),
        ptLow_(channel.ptLow),
        ptHigh_(channel.ptHigh) {}
  ChannelKind kind() const override { return ChannelKind::PfFraction; }

  void addChi2(const double* par, const double*, double& chi2, int& nPoints) const override {
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      if (!used(i)) continue;
      const double df = model_.pfFraction(fraction_, pt_[i], par);
      const double chi = std::max(std::fabs(y_[i] - df) - 0.01 * delta_, 0.) / sigma_[i];
      chi2 += chi * chi;
      ++nPoints;
    }
  }

  std::vector<PointResult> evaluate(const double* par, const double*) const override {
    std::vector<PointResult> points(pt_.size());
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      PointResult& p = points[i];
      p.pt = pt_[i];
      p.ptError = ptError_[i];
      p.prediction = model_.pfFraction(fraction_, pt_[i], par);
      p.value = y_[i];
      p.sigma = sigma_[i];
      p.used = used(i);
      if (p.used) {
        const double excess = std::max(std::fabs(p.value - p.prediction) - 0.01 * delta_, 0.);
        p.chi = (p.value > p.prediction ? excess : -excess) / p.sigma;
      }
    }
    return points;
  }

  std::vector<double> generate(const double* par, const double*, TRandom& random) const override {
    std::vector<double> y(pt_.size());
    for (std::size_t i = 0; i != pt_.size(); ++i) {
      y[i] = model_.pfFraction(fraction_, pt_[i], par) + random.Gaus(0, sigma_[i]);
    }
    return y;
  }

 private:
  bool used(std::size_t i) const { return pt_[i] >= ptMin_ && pt_[i] > ptLow_ && pt_[i] < ptHigh_; }
  int fraction_;
  double delta_, ptLow_, ptHigh_;
};

}  // namespace

std::unique_ptr<Channel> makeChannel(const Workspace& workspace, std::size_t index,
                                     const PhysicsModel& model) {
  const WorkspaceChannel& wsChannel = workspace.channels()[index];
  const double ptMin = workspace.card().ptMin;
  std::unique_ptr<Channel> channel;
  switch (wsChannel.kind) {
    case ChannelKind::Response:
      channel = std::make_unique<ResponseChannel>(wsChannel, model, ptMin);
      break;
    case ChannelKind::Multijet:
      channel = std::make_unique<MultijetChannel>(wsChannel, model, ptMin);
      break;
    case ChannelKind::PfFraction:
      channel = std::make_unique<PfFractionChannel>(wsChannel, model, ptMin);
      break;
  }

  // Size of each nuisance at each point: the bin content of its histogram at the
  // point's pT (shape) or the constant from the card (flat).
  const auto& nuisances = workspace.nuisances();
  for (std::size_t n = 0; n != nuisances.size(); ++n) {
    const WorkspaceNuisance& nuisance = nuisances[n];
    if (!nuisance.values[index]) continue;
    for (std::size_t i = 0; i != channel->pt_.size(); ++i) {
      double size = *nuisance.values[index];
      if (nuisance.type == CardNuisance::Type::Shape) {
        const TH1D& hist = *nuisance.shapes[index];
        const double content = hist.GetBinContent(hist.FindFixBin(channel->pt_[i]));
        size = size == 1. ? content : size * content;
      }
      channel->shifts_[i].emplace_back(static_cast<int>(n), size);
    }
  }
  return channel;
}
