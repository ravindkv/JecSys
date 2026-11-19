#pragma once
// The fit options that jecFit and runFit have in common: declared once, parsed once.
#include "CommandLine.h"
#include "FitMethod.h"

namespace fitCommandLine {
void addOptions(CommandLine& cli);
FitOptions parse(const CommandLine& cli);  // throws JecSysError on a malformed value
}  // namespace fitCommandLine
