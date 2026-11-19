#pragma once
// The fixed functional forms used by the JES models, as pure functions of pT. The
// numbers come from the model's JSON file; the forms are code. Each form is the
// formula string of the corresponding TF1 in the old globalFitL3Res.C, written with
// the same operations in the same order so that the values are identical.
#include <string>
#include <vector>

class ShapeFunction {
 public:
  enum class Form {
    Const,            // p0
    Spr,              // p0+p1*pow(x/p2,p3)/(1+pow(x/p2,p3))*(1-pow(x/p2,-p3))      single-pion response
    Trk,              // p0+p1*pow(x/208.,p2)+p3/x                                  tracking
    Frag,             // p0+p1*(1+(pow(x/p2,p3)-1)/(pow(x/p2,p3)+1))+p4/x+p5*log(x)/x
    L1,               // 1-(p0+p1*log(x)+p2*pow(log(x),2))/x                        offset
    Flavor,           // 1+0.01*(p0+p1*(pow(0.01*x,p2)-1))
    SigmoidPow,       // p0+p1*(1+(pow(x/p2,p3)-1)/(pow(x/p2,p3)+1))+p4*pow(x,p5)
    SigmoidPow03Inv,  // p0+p1*(1+(pow(x/p2,p3)-1)/(pow(x/p2,p3)+1))+p4*pow(x,+0.3)+p5/x
    SigmoidPowInv,    // p0+p1*(1+(pow(x/p2,p3)-1)/(pow(x/p2,p3)+1))+p4*pow(x,p5)+p6/x
    PowInv,           // p0+p1*pow(x,p2)+p3/x
  };

  ShapeFunction() = default;
  // Throws JecSysError if the form is unknown or the number of parameters is wrong.
  ShapeFunction(const std::string& form, std::vector<double> par, const std::string& where);

  double operator()(double x) const;

 private:
  Form form_ = Form::Const;
  std::vector<double> p_{0};
};
