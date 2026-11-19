#include "Chi2Function.h"

#include "JecSysError.h"

Chi2Function::Chi2Function(const Workspace& workspace, const PhysicsModel& model,
                           const std::vector<bool>& maskedChannels)
    : model_(model), masked_(maskedChannels) {
  const std::size_t nChannels = workspace.channels().size();
  if (masked_.empty()) masked_.assign(nChannels, false);
  if (masked_.size() != nChannels) throw JecSysError("Chi2Function: mask has the wrong size");
  for (std::size_t i = 0; i != nChannels; ++i) channels_.push_back(makeChannel(workspace, i, model));
  for (const FitParameter& p : model.parameters()) penalized_.push_back(p.penalized);
  nModelPar_ = static_cast<int>(model.parameters().size());
  nNuisances_ = static_cast<int>(workspace.nuisances().size());
}

double Chi2Function::operator()(const double* par) const {
  const double* theta = par + nModelPar_;
  double chi2 = 0;
  int nPoints = 0;
  for (std::size_t i = 0; i != channels_.size(); ++i) {
    if (!masked_[i]) channels_[i]->addChi2(par, theta, chi2, nPoints);
  }
  for (int i = 0; i != nNuisances_; ++i) chi2 += theta[i] * theta[i];
  for (int i = 0; i != nModelPar_; ++i) {
    if (penalized_[i]) chi2 += par[i] * par[i];
  }
  return chi2;
}

Chi2Breakdown Chi2Function::breakdown(const double* par) const {
  const double* theta = par + nModelPar_;
  Chi2Breakdown result;
  result.channelChi2.assign(channels_.size(), 0.);
  result.channelPoints.assign(channels_.size(), 0);
  for (std::size_t i = 0; i != channels_.size(); ++i) {
    if (masked_[i]) continue;
    channels_[i]->addChi2(par, theta, result.channelChi2[i], result.channelPoints[i]);
    result.nPoints += result.channelPoints[i];
  }
  for (int i = 0; i != nNuisances_; ++i) result.nuisanceChi2 += theta[i] * theta[i];
  for (int i = 0; i != nModelPar_; ++i) {
    if (!penalized_[i]) continue;
    result.penaltyChi2 += par[i] * par[i];
    ++result.nPoints;
  }
  result.total = (*this)(par);  // same summation order as the fit
  return result;
}

void Chi2Function::setData(const std::vector<std::vector<double>>& data) {
  if (data.size() != channels_.size()) throw JecSysError("Chi2Function: wrong number of toy channels");
  for (std::size_t i = 0; i != channels_.size(); ++i) channels_[i]->setData(data[i]);
}

std::vector<std::vector<double>> Chi2Function::data() const {
  std::vector<std::vector<double>> result;
  for (const auto& channel : channels_) result.push_back(channel->data());
  return result;
}
