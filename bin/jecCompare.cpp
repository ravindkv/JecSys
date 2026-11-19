// jecCompare: comparison of the objects below one directory of a ROOT file with the
// objects of the same name in another file. Every cycle of a key is compared with the
// same cycle in the other file. Two modes:
//   numbers (default)  histograms, graphs, matrices and vectors agree within a relative
//                      tolerance; other types are skipped;
//   --bytes            every object has identical streamed bytes (numbers, names,
//                      titles and draw attributes), whatever its type.
// Used by the parity tests: test/parity.sh (the fit against the old macro chain, numbers)
// and the input creation against golden snapshots (--bytes --complete).
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>

#include "CommandLine.h"
#include "InputFile.h"
#include "JecSysError.h"
#include "TBufferFile.h"
#include "TClass.h"
#include "TGraphErrors.h"
#include "TH1.h"
#include "TKey.h"
#include "TMatrixD.h"
#include "TVectorD.h"
#include "TextTools.h"

namespace {

struct Options {
  double tolerance = 1e-6, floor = 1e-9;
  bool bytes = false, complete = false, verbose = false;
  std::string allowMissing;
};

struct Summary {
  int compared = 0, identicalBytes = 0, missing = 0, failed = 0, skipped = 0;
  double worst = 0;
  std::string worstName;
};

// A key of a file: its path below the compared directory and its cycle.
using KeyName = std::pair<std::string, short>;

std::string displayName(const KeyName& name) { return name.first + ";" + std::to_string(name.second); }

// Difference of two numbers relative to their size, with an absolute floor so that
// numbers that should be 0 can differ by rounding noise. Two NaNs agree; a NaN or an
// infinity against another number is an infinite difference.
double difference(double a, double b, double floor) {
  if (a == b || (std::isnan(a) && std::isnan(b))) return 0;
  if (!std::isfinite(a) || !std::isfinite(b)) return std::numeric_limits<double>::infinity();
  return std::fabs(a - b) / std::max({std::fabs(a), std::fabs(b), floor});
}

// Returns the largest difference, -1 if the two objects cannot be compared (size or
// type), -2 if the type is not compared numerically.
double compareNumbers(const TObject& a, const TObject& b, double floor) {
  double worst = 0;
  const auto update = [&](double x, double y) { worst = std::max(worst, difference(x, y, floor)); };
  if (const auto* ha = dynamic_cast<const TH1*>(&a)) {
    const auto* hb = dynamic_cast<const TH1*>(&b);
    if (!hb || ha->GetNcells() != hb->GetNcells()) return -1;
    for (int i = 0; i != ha->GetNcells(); ++i) {
      update(ha->GetBinContent(i), hb->GetBinContent(i));
      update(ha->GetBinError(i), hb->GetBinError(i));
    }
    return worst;
  }
  if (const auto* ga = dynamic_cast<const TGraphErrors*>(&a)) {
    const auto* gb = dynamic_cast<const TGraphErrors*>(&b);
    if (!gb || ga->GetN() != gb->GetN()) return -1;
    for (int i = 0; i != ga->GetN(); ++i) {
      update(ga->GetX()[i], gb->GetX()[i]);
      update(ga->GetY()[i], gb->GetY()[i]);
      update(ga->GetEY()[i], gb->GetEY()[i]);
    }
    return worst;
  }
  if (const auto* ma = dynamic_cast<const TMatrixD*>(&a)) {
    const auto* mb = dynamic_cast<const TMatrixD*>(&b);
    if (!mb || ma->GetNrows() != mb->GetNrows() || ma->GetNcols() != mb->GetNcols()) return -1;
    for (int i = 0; i != ma->GetNrows(); ++i) {
      for (int j = 0; j != ma->GetNcols(); ++j) update((*ma)[i][j], (*mb)[i][j]);
    }
    return worst;
  }
  if (const auto* va = dynamic_cast<const TVectorD*>(&a)) {
    const auto* vb = dynamic_cast<const TVectorD*>(&b);
    if (!vb || va->GetNrows() != vb->GetNrows()) return -1;
    for (int i = 0; i != va->GetNrows(); ++i) update((*va)[i], (*vb)[i]);
    return worst;
  }
  return -2;
}

// The object as ROOT streams it into a file (before compression).
std::string streamedBytes(TObject& object) {
  TBufferFile buffer(TBuffer::kWrite);
  object.Streamer(buffer);
  return std::string(buffer.Buffer(), buffer.Length());
}

// Every key below `dir`, every cycle; directories are entered, not listed.
void collectKeys(TDirectory& dir, const std::string& path, std::map<KeyName, TKey*>& keys) {
  for (TObject* keyObject : *dir.GetListOfKeys()) {
    auto* key = static_cast<TKey*>(keyObject);
    const std::string name = key->GetName();
    const std::string full = path.empty() ? name : path + "/" + name;
    const TClass* type = TClass::GetClass(key->GetClassName());
    if (type && type->InheritsFrom(TDirectory::Class())) {
      if (TDirectory* sub = dir.GetDirectory(name.c_str())) collectKeys(*sub, full, keys);
      continue;
    }
    keys[{full, key->GetCycle()}] = key;
  }
}

void report(bool ok, const std::string& name, const std::string& detail, const Options& options) {
  if (!ok || options.verbose) std::cout << (ok ? "  ok   " : "  FAIL ") << name << "  " << detail << "\n";
}

void compareObjects(TKey& keyA, TKey& keyB, const std::string& name, const Options& options, Summary& summary) {
  const std::unique_ptr<TObject> a(keyA.ReadObj()), b(keyB.ReadObj());
  if (!a || !b) throw JecSysError("cannot read " + name);
  if (std::string(a->ClassName()) != b->ClassName()) {
    ++summary.compared;
    ++summary.failed;
    report(false, name, std::string("class ") + a->ClassName() + " vs " + b->ClassName(), options);
    return;
  }
  const bool sameBytes = options.bytes && streamedBytes(*a) == streamedBytes(*b);
  const double worst = sameBytes ? 0 : compareNumbers(*a, *b, options.floor);
  if (worst == -2 && !options.bytes) {
    ++summary.skipped;
    return;
  }
  ++summary.compared;
  if (sameBytes) ++summary.identicalBytes;
  const bool numbersOk = worst >= 0 && worst <= options.tolerance;
  const bool ok = options.bytes ? sameBytes : numbersOk;
  if (!ok) ++summary.failed;
  if (worst > summary.worst) {
    summary.worst = worst;
    summary.worstName = name;
  }
  std::string detail = worst == -1   ? "different size or type"
                       : worst == -2 ? "type not compared numerically"
                                     : textTools::formatDouble(worst);
  if (options.bytes) {
    detail = sameBytes ? "identical bytes" : "bytes differ; numbers: " + detail;
  }
  report(ok, name, detail, options);
}

bool allowedMissing(const KeyName& name, const Options& options) {
  return !options.allowMissing.empty() && textTools::regexMatch(options.allowMissing, name.first);
}

Summary compareDirectories(TDirectory& dirA, TDirectory& dirB, const Options& options) {
  std::map<KeyName, TKey*> keysA, keysB;
  collectKeys(dirA, "", keysA);
  collectKeys(dirB, "", keysB);
  Summary summary;
  for (const auto& [name, keyA] : keysA) {
    const auto itB = keysB.find(name);
    if (itB != keysB.end()) {
      compareObjects(*keyA, *itB->second, displayName(name), options, summary);
    } else if (allowedMissing(name, options)) {
      ++summary.skipped;
    } else {
      ++summary.missing;
      std::cout << "  MISSING in reference: " << displayName(name) << "\n";
    }
  }
  if (options.complete) {
    for (const auto& [name, keyB] : keysB) {
      if (keysA.count(name)) continue;
      if (allowedMissing(name, options)) {
        ++summary.skipped;
      } else {
        ++summary.missing;
        std::cout << "  MISSING in new: " << displayName(name) << "\n";
      }
    }
  }
  return summary;
}

TDirectory& directory(const InputFile& file, const std::string& name) {
  if (name.empty() || name == "/") return file.file();
  TDirectory* dir = file.file().GetDirectory(name.c_str());
  if (!dir) throw JecSysError("no directory " + name + " in " + file.path());
  return *dir;
}

}  // namespace

int main(int argc, char** argv) {
  return runMain("jecCompare", [&]() {
    CommandLine cli("jecCompare", "jecCompare <new.root> [<dir>] <reference.root> [<dir>] [options]",
                    "compare every object (every cycle) below a directory (default: the whole file)\n"
                    "with the same object in a reference file");
    cli.addOption("tolerance", 't', "x", "largest allowed relative difference (default: 1e-6)");
    cli.addOption("floor", 0, "x", "numbers smaller than this are compared absolutely (default: 1e-9)");
    cli.addOption("bytes", 0, "",
                  "require identical streamed bytes (numbers, names, titles, attributes), for every type");
    cli.addOption("complete", 0, "", "also fail on objects of the reference that are missing in <new.root>");
    cli.addOption("allowMissing", 0, "regex", "objects (path without cycle) that may be absent from the other file");
    cli.addOption("verbose", 'v', "", "print every object");
    cli.setFooter("Exit code 0 if every object of <new.root>:<dir> exists in the reference and agrees.");
    cli.parse(argc, argv);
    const auto& args = cli.positional();
    if (cli.helpRequested() || (args.size() != 2 && args.size() != 4)) {
      std::cout << cli.help();
      return cli.helpRequested() ? 0 : 1;
    }
    Options options;
    options.tolerance = textTools::toDouble(cli.value("tolerance", "1e-6"), "--tolerance");
    options.floor = textTools::toDouble(cli.value("floor", "1e-9"), "--floor");
    options.bytes = cli.has("bytes");
    options.complete = cli.has("complete");
    options.verbose = cli.has("verbose");
    options.allowMissing = cli.value("allowMissing");

    TH1::AddDirectory(false);  // objects read for the comparison stay out of the files
    const bool withDirs = args.size() == 4;
    const InputFile fileA(args[0]), fileB(args[withDirs ? 2 : 1]);
    TDirectory& dirA = directory(fileA, withDirs ? args[1] : "");
    TDirectory& dirB = directory(fileB, withDirs ? args[3] : "");

    const Summary summary = compareDirectories(dirA, dirB, options);
    std::cout << "  " << summary.compared << " objects compared";
    if (options.bytes) std::cout << " (" << summary.identicalBytes << " with identical bytes)";
    std::cout << ", " << summary.failed << " differ, " << summary.missing << " missing, " << summary.skipped
              << " skipped; largest difference " << textTools::formatDouble(summary.worst)
              << (summary.worstName.empty() ? "" : " (" + summary.worstName + ")") << "\n";
    return summary.failed == 0 && summary.missing == 0 && summary.compared > 0 ? 0 : 1;
  });
}
