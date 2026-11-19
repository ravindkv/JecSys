// Unit tests of the fit engine on a small hand-made card: the chi2 of each channel
// kind is compared with the formula written out by hand, then masking, freezing and
// the workspace round trip are checked.
#include <cmath>
#include <fstream>

#include "Chi2Function.h"
#include "ConfigFile.h"
#include "FitMinimizer.h"
#include "OutputFile.h"
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TestTools.h"
#include "TextTools.h"
#include "Workspace.h"

namespace {

const char* kDir = "obj/test/tmp";

const char* kCard = R"(epoch test
corLevel L1L2L3Res
etaBin eta00-13
ptMin 15
shapes data * shapes.root data/$CHANNEL
shapes *    * shapes.root nuisance/$NUISANCE
aux crecoil ptchs_multijet shapes.root aux/crecoil
aux jesRef  ptchs_*        shapes.root aux/jesRef
channel  ptchs_zjet ptchs_multijet chf_pfjet
kind     response   multijet       pfFraction
fraction -          -              chf
delta    -          -              0.2
ptRange  -          -              40:500
fsr_z    shape 1     -     -
scale_z  flat  0.002 -     -
both     flat  0.001 0.003 -
)";

TGraphErrors graph(std::initializer_list<double> x, std::initializer_list<double> y, double error) {
  TGraphErrors g(static_cast<int>(x.size()));
  for (std::size_t i = 0; i != x.size(); ++i) {
    g.SetPoint(static_cast<int>(i), x.begin()[i], y.begin()[i]);
    g.SetPointError(static_cast<int>(i), 0., error);
  }
  return g;
}

}  // namespace

int main() {
  using testTools::close;
  TH1::AddDirectory(false);
  textTools::makeDirs(kDir);

  // ---- inputs on disk ----
  TH1D fsr("fsr_z", "", 2, 10., 1000.);  // bins [10,505), [505,1000)
  fsr.SetBinContent(1, 0.01);
  fsr.SetBinContent(2, -0.02);
  TH1D jesRef("jesRef", "", 1, 5., 5000.);
  jesRef.SetBinContent(1, 1.02);
  {
    OutputFile out(std::string(kDir) + "/shapes.root");
    out.write("data/ptchs_zjet", graph({10., 50., 600.}, {0.97, 0.98, 1.01}, 0.01));  // 10 GeV < ptMin
    out.write("data/ptchs_multijet", graph({800.}, {1.005}, 0.004));
    out.write("data/chf_pfjet", graph({30., 100., 200.}, {0.004, 0.0105, -0.0005}, 0.002));  // 30 GeV out of range
    out.write("aux/crecoil", graph({800.}, {0.45}, 0.));
    out.write("aux/jesRef", jesRef);
    out.write("nuisance/fsr_z", fsr);
    out.commit();
  }
  {
    std::ofstream card(std::string(kDir) + "/datacard.txt");
    card << kCard;
  }

  const std::string modelConfig = ConfigFile("config/JesModel9.json").resolve("", "").dump();
  const auto model = makePhysicsModel("JesModel9", modelConfig);
  const Datacard card = Datacard::read(std::string(kDir) + "/datacard.txt");
  const Workspace built = Workspace::fromCard(card, "JesModel9", modelConfig);
  built.write(std::string(kDir) + "/workspace.root", "datacard.txt");
  const Workspace ws = Workspace::read(std::string(kDir) + "/workspace.root");
  CHECK(ws.channels().size() == 3 && ws.nuisances().size() == 3);
  CHECK(ws.modelName() == "JesModel9");

  // ---- chi2 against the formula written by hand ----
  const Chi2Function chi2(ws, *model);
  CHECK(chi2.nModelPar() == 9 && chi2.nNuisances() == 3 && chi2.nPar() == 12);
  const double par[12] = {1.2, 0.8, 1.1, 0.9, 1.3, 0.2, -0.1, 0.7, 0.3, /* theta: */ 0.5, -1.0, 2.0};
  const double* theta = par + 9;
  // jesRef is one flat bin, where TH1::Interpolate returns the bin content.
  const auto jes = [&](double pt) { return model->response(pt, par) / 1.02; };

  double expected = 0;
  // response: (data + shifts - model) / sigma, points below ptMin skipped
  expected += std::pow((0.98 + theta[0] * 0.01 + theta[1] * 0.002 + theta[2] * 0.001 - jes(50.)) / 0.01, 2);
  expected += std::pow((1.01 + theta[0] * -0.02 + theta[1] * 0.002 + theta[2] * 0.001 - jes(600.)) / 0.01, 2);
  // multijet: data and sigma scaled by the model at the recoil pT
  const double fitRef = jes(0.45 * 800.);
  expected += std::pow((1.005 * fitRef + theta[2] * 0.003 - jes(800.)) / (0.004 * fitRef), 2);
  // PF fraction: dead zone of 0.2%, open pT interval
  for (const auto& [pt, data] : {std::pair{100., 0.0105}, std::pair{200., -0.0005}}) {
    const double df = model->pfFraction(model->fractionIndex("chf"), pt, par);
    expected += std::pow(std::max(std::fabs(data - df) - 0.01 * 0.2, 0.) / 0.002, 2);
  }
  double constraints = 0;
  for (int i = 0; i != 3; ++i) constraints += theta[i] * theta[i];
  for (int i = 0; i != 9; ++i) constraints += par[i] * par[i];
  CHECK(close(chi2(par), expected + constraints));

  const Chi2Breakdown breakdown = chi2.breakdown(par);
  CHECK(breakdown.channelPoints[0] == 2 && breakdown.channelPoints[1] == 1 && breakdown.channelPoints[2] == 2);
  CHECK(breakdown.nPoints == 5 + 9);
  CHECK(close(breakdown.total, chi2(par)));
  CHECK(close(breakdown.channelChi2[0] + breakdown.channelChi2[1] + breakdown.channelChi2[2] +
                  breakdown.nuisanceChi2 + breakdown.penaltyChi2,
              breakdown.total));

  // ---- post-fit points agree with the chi2 ----
  const auto points = chi2.channels()[1]->evaluate(par, theta);
  CHECK(points.size() == 1 && points[0].hasRecoil && close(points[0].recoilPt, 360.));
  CHECK(close(points[0].chi * points[0].chi, breakdown.channelChi2[1]));

  // ---- masking removes exactly that channel ----
  const Chi2Function masked(ws, *model, {false, true, false});
  CHECK(close(masked(par), chi2(par) - breakdown.channelChi2[1]));

  // ---- minimisation: converges, improves, respects frozen parameters ----
  std::vector<ParameterSpec> specs = makeParameterSpecs(*model, ws);
  CHECK(specs.size() == 12 && specs[9].name == "fsr_z" && specs[9].isNuisance);
  std::vector<double> start;
  for (const auto& spec : specs) start.push_back(spec.start);
  const FitResult fit = FitMinimizer::minimize(chi2, specs);
  CHECK(fit.status == 0 && fit.nFree == 12);
  CHECK(fit.chi2 < chi2(start.data()));
  CHECK(close(fit.chi2, chi2(fit.values.data())));
  CHECK(fit.covariance.GetNrows() == 12 && fit.covariance[0][0] > 0);

  specs[0].fixed = true;
  specs[0].start = 0.5;
  const FitResult frozen = FitMinimizer::minimize(chi2, specs);
  CHECK(frozen.nFree == 11 && frozen.values[0] == 0.5 && frozen.errors[0] == 0);
  CHECK(frozen.covariance[0][0] == 0 && frozen.covariance[1][1] > 0);
  CHECK(frozen.chi2 >= fit.chi2 - 1e-6);

  // ---- toys: generated data replace the data and can be restored ----
  Chi2Function toyChi2(ws, *model);
  const auto original = toyChi2.data();
  CHECK(original.size() == 3 && original[0].size() == 3);
  auto shifted = original;
  shifted[0][1] += 0.01;
  toyChi2.setData(shifted);
  CHECK(!close(toyChi2(par), chi2(par)));
  toyChi2.setData(original);
  CHECK(close(toyChi2(par), chi2(par)));

  return testTools::finish("testChi2");
}
