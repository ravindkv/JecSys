#pragma once
// Graph helpers of the input creation: the parts of tools.C that the macros use, with the
// same arithmetic (so the results are the same to the last bit).
#include <memory>

class TGraphErrors;

namespace graphTools {

// sqrt(a*a + b*b) (tools::oplus).
double oplus(double a, double b);

// x, y, ex, ey of point i (tools::GetPoint).
void getPoint(const TGraphErrors& graph, int i, double& x, double& y, double& ex, double& ey);
// SetPoint and SetPointError of point i (tools::SetPoint).
void setPoint(TGraphErrors& graph, int i, double x, double y, double ex, double ey);

// c1*g1 - c2*g2 at the pairs of points that are closer to each other than to the
// neighbours of either one (tools::diffGraphs). Name "diff_<g1>_<g2>"; marker style,
// marker and line colour of g1.
std::unique_ptr<TGraphErrors> diffGraphs(const TGraphErrors& g1, const TGraphErrors& g2, double c1 = 1,
                                         double c2 = 1);

// g1/g2 at the same pairs (tools::ratioGraphs(g1, g2, erry)): y = y1/y2 (0 if y2 == 0),
// ey = oplus(ey1/y1, ey2/y2) * |y| * erry. Name "ratio_<g1>_<g2>".
std::unique_ptr<TGraphErrors> ratioGraphs(const TGraphErrors& g1, const TGraphErrors& g2, double erry = 1);

// Inverse-variance average of the points of g1 and g2 that are each other's closest in x
// (tools::mergeGraphs). Default name and attributes.
std::unique_ptr<TGraphErrors> mergeGraphs(const TGraphErrors& g1, const TGraphErrors& g2);

}  // namespace graphTools
