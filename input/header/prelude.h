#pragma once
// Force-included (g++ -include) when the Makefile compiles the CMS JEC classes in
// header/CondFormats/ (third-party code, not edited). Two of their files use cout and
// cerr without std::, which cling's implicit "using namespace std" hid under ACLiC.
// Never include this file from our own code.
#include <iostream>
using namespace std;
