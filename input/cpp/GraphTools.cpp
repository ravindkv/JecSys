#include "GraphTools.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "TGraphErrors.h"
#include "TString.h"

namespace graphTools {

namespace {

// An empty graph with the name and the marker and line attributes tools.C copies from g1.
std::unique_ptr<TGraphErrors> newGraphLike(const TGraphErrors& g1, const char* name) {
  auto graph = std::make_unique<TGraphErrors>(0);
  graph->SetName(name);
  graph->SetMarkerStyle(g1.GetMarkerStyle());
  graph->SetMarkerColor(g1.GetMarkerColor());
  graph->SetLineColor(g1.GetLineColor());
  return graph;
}

// Calls `pair(x1, y1, ey1, x2, y2, ey2)` for the points of g1 and g2 that are closer to
// each other than to the neighbours of either one; both graphs are sorted in x.
template <class Pair>
void forClosestPairs(const TGraphErrors& g1, const TGraphErrors& g2, Pair pair) {
  for (int i = 0, j = 0; (i != g1.GetN() && j != g2.GetN());) {
    double x1m, x1p, x2m, x2p, y, ex, ey;
    getPoint(g1, std::max(i - 1, 0), x1m, y, ex, ey);
    getPoint(g1, std::min(i + 1, g1.GetN() - 1), x1p, y, ex, ey);
    getPoint(g2, std::max(j - 1, 0), x2m, y, ex, ey);
    getPoint(g2, std::min(j + 1, g2.GetN() - 1), x2p, y, ex, ey);
    double x1, y1, ex1, ey1;
    getPoint(g1, i, x1, y1, ex1, ey1);
    double x2, y2, ex2, ey2;
    getPoint(g2, j, x2, y2, ex2, ey2);
    if (std::fabs(x1 - x2) <= std::fabs(x1 - x2m) && std::fabs(x1 - x2) <= std::fabs(x1 - x2p) &&
        std::fabs(x1 - x2) <= std::fabs(x2 - x1m) && std::fabs(x1 - x2) <= std::fabs(x2 - x1p)) {
      pair(x1, y1, ey1, x2, y2, ey2);
      ++i, ++j;
    } else {
      (x1 < x2 ? ++i : ++j);
    }
  }
}

}  // namespace

double oplus(double a, double b) { return std::sqrt(a * a + b * b); }

void getPoint(const TGraphErrors& graph, int i, double& x, double& y, double& ex, double& ey) {
  graph.GetPoint(i, x, y);
  ex = graph.GetErrorX(i);
  ey = graph.GetErrorY(i);
}

void setPoint(TGraphErrors& graph, int i, double x, double y, double ex, double ey) {
  graph.SetPoint(i, x, y);
  graph.SetPointError(i, ex, ey);
}

std::unique_ptr<TGraphErrors> diffGraphs(const TGraphErrors& g1, const TGraphErrors& g2, double c1, double c2) {
  auto graph = newGraphLike(g1, Form("diff_%s_%s", g1.GetName(), g2.GetName()));
  forClosestPairs(g1, g2, [&](double x1, double y1, double ey1, double x2, double y2, double ey2) {
    setPoint(*graph, graph->GetN(), 0.5 * (x1 + x2), c1 * y1 - c2 * y2, 0.5 * std::fabs(x1 - x2),
             oplus(c1 * ey1, c2 * ey2));
  });
  return graph;
}

std::unique_ptr<TGraphErrors> ratioGraphs(const TGraphErrors& g1, const TGraphErrors& g2, double erry) {
  auto graph = newGraphLike(g1, Form("ratio_%s_%s", g1.GetName(), g2.GetName()));
  forClosestPairs(g1, g2, [&](double x1, double y1, double ey1, double x2, double y2, double ey2) {
    setPoint(*graph, graph->GetN(), 0.5 * (x1 + x2), (y2 ? y1 / y2 : 0.), 0.5 * std::fabs(x1 - x2),
             oplus(ey1 / y1, ey2 / y2) * std::fabs(y2 ? y1 / y2 : 0.) * erry);
  });
  return graph;
}

std::unique_ptr<TGraphErrors> mergeGraphs(const TGraphErrors& g1, const TGraphErrors& g2) {
  // Find the j closest to each i and the i closest to each j; pair them when both agree.
  std::map<int, int> itoj, jtoi;
  std::map<int, double> idxmin, jdxmin;
  for (int i = 0; i != g1.GetN(); ++i) {
    for (int j = 0; j != g2.GetN(); ++j) {
      const double dx = std::fabs(g1.GetX()[i] - g2.GetX()[j]);
      if (dx < idxmin[i] || j == 0) {
        itoj[i] = j;
        idxmin[i] = dx;
      }
      if (dx < jdxmin[j] || i == 0) {
        jtoi[j] = i;
        jdxmin[j] = dx;
      }
    }
  }
  auto graph = std::make_unique<TGraphErrors>(0);
  for (int i = 0; i != g1.GetN(); ++i) {
    const int j = itoj[i];
    if (jtoi[j] == i && g2.GetN() > 0) {
      const int n = graph->GetN();
      double w1(0.5), w2(0.5);
      if (g2.GetN() == 0) {
        w1 = 1, w2 = 0;
      } else if (g1.GetN() == 0) {
        w1 = 0, w2 = 1;
      } else {
        const double n1 = 1. / (g1.GetEY()[i] * g1.GetEY()[i]);  // pow(x,2) of tools.C, folded to x*x
        const double n2 = 1. / (g2.GetEY()[j] * g2.GetEY()[j]);
        w1 = n1 / (n1 + n2);
        w2 = n2 / (n1 + n2);
      }
      const double y = (g1.GetY()[i] * w1 + g2.GetY()[j] * w2);
      const double ey1 = g1.GetEY()[i] * w1, ey2 = g2.GetEY()[j] * w2;
      const double dy = std::sqrt(ey1 * ey1 + ey2 * ey2);
      const double x = (g1.GetX()[i] * w1 + g2.GetX()[j] * w2);
      const double ex1 = g1.GetEX()[i] * w1, ex2 = g2.GetEX()[j] * w2;
      const double dx = std::sqrt(ex1 * ex1 + ex2 * ex2);
      graph->SetPoint(n, x, y);
      graph->SetPointError(n, dx, dy);
    }
  }
  return graph;
}

}  // namespace graphTools
