// Unit tests of GraphTools (the graph helpers of tools.C) on small hand-made graphs.
// GraphTools was also checked bit for bit against tools.C on 9000 random graph pairs
// (2026-11-19, before tools.C is deleted).
#include <array>
#include <cmath>
#include <string>

#include "GraphTools.h"
#include "TGraphErrors.h"
#include "TestTools.h"

namespace {

using testTools::close;

// A graph from points {x, y, ex, ey}.
TGraphErrors makeGraph(const char* name, std::initializer_list<std::array<double, 4>> points) {
  TGraphErrors graph(0);
  graph.SetName(name);
  int i = 0;
  for (const auto& p : points) graphTools::setPoint(graph, i++, p[0], p[1], p[2], p[3]);
  return graph;
}

bool point(const TGraphErrors& g, int i, double x, double y, double ex, double ey) {
  double px, py, pex, pey;
  graphTools::getPoint(g, i, px, py, pex, pey);
  return close(px, x) && close(py, y) && close(pex, ex) && close(pey, ey);
}

}  // namespace

int main() {
  CHECK(graphTools::oplus(3, 4) == 5);

  // Pairing: (1, 1.1) and (3, 2.9) are each other's closest; 2 is closer to 2.9's
  // neighbour 3 than to 2.9, so it is left out.
  TGraphErrors g1 = makeGraph("g1", {{1, 10, 0, 1}, {2, 20, 0, 2}, {3, 30, 0, 3}});
  const TGraphErrors g2 = makeGraph("g2", {{1.1, 4, 0, 3}, {2.9, 6, 0, 4}});
  g1.SetMarkerStyle(21);
  g1.SetMarkerColor(2);
  g1.SetLineColor(3);

  const auto diff = graphTools::diffGraphs(g1, g2);
  CHECK(std::string(diff->GetName()) == "diff_g1_g2");
  CHECK(diff->GetN() == 2);
  CHECK(point(*diff, 0, 1.05, 6, 0.05, std::sqrt(10.)));
  CHECK(point(*diff, 1, 2.95, 24, 0.05, 5));
  CHECK(diff->GetMarkerStyle() == 21 && diff->GetMarkerColor() == 2 && diff->GetLineColor() == 3);

  const auto sum = graphTools::diffGraphs(g1, g2, 1.0, -1.0);  // c1*g1 - c2*g2 = g1 + g2
  CHECK(point(*sum, 0, 1.05, 14, 0.05, std::sqrt(10.)));

  const auto ratio = graphTools::ratioGraphs(g1, g2, 2.0);
  CHECK(std::string(ratio->GetName()) == "ratio_g1_g2");
  CHECK(point(*ratio, 0, 1.05, 2.5, 0.05, graphTools::oplus(1. / 10, 3. / 4) * 2.5 * 2.0));
  CHECK(point(*ratio, 1, 2.95, 5, 0.05, graphTools::oplus(3. / 30, 4. / 6) * 5 * 2.0));
  const auto ratioZero = graphTools::ratioGraphs(g1, makeGraph("z", {{1, 0, 0, 1}}));
  CHECK(ratioZero->GetN() == 1 && ratioZero->GetY()[0] == 0);  // y2 == 0 gives 0

  // Merge: only points that are each other's closest are averaged (weights 1/ey^2).
  const auto merged = graphTools::mergeGraphs(makeGraph("a", {{1, 2, 0.2, 1}, {2, 4, 0.2, 1}}),
                                              makeGraph("b", {{1.1, 4, 0.4, 1}, {5, 8, 0.4, 2}}));
  CHECK(merged->GetN() == 1);
  CHECK(point(*merged, 0, 1.05, 3, std::sqrt(0.01 + 0.04), std::sqrt(0.5)));
  const auto weighted = graphTools::mergeGraphs(makeGraph("a", {{1, 2, 0, 1}}), makeGraph("b", {{1, 5, 0, 2}}));
  CHECK(point(*weighted, 0, 1, (2 * 0.8 + 5 * 0.2), 0, std::sqrt(0.8 * 0.8 + 0.4 * 0.4)));
  CHECK(graphTools::mergeGraphs(g1, makeGraph("e", {}))->GetN() == 0);
  return testTools::finish("testGraphTools");
}
