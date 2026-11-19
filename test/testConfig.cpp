// Unit tests of the configuration infrastructure: formatTemplate (the name templates of
// the configs, checked against Python's str.format), the override-key check of
// ConfigFile, the roots of Inputs, and every config/*.json resolving for every epoch.
// Run from the repository root (make test).
#include <filesystem>
#include <fstream>
#include <limits>

#include "ConfigFile.h"
#include "Inputs.h"
#include "TestTools.h"
#include "TextTools.h"

namespace {

using textTools::formatTemplate;
using textTools::pythonRepr;
using testTools::throwsError;

// A ConfigFile read from `text` (written to obj/test/).
ConfigFile configFrom(const std::string& text) {
  const std::string path = "obj/test/testConfig.json";
  std::ofstream(path) << text;
  return ConfigFile(path);
}

void testFormatTemplate() {
  // Expected strings are the output of Python's str.format for the same values.
  CHECK(formatTemplate("{tierName}/eta_{eta1x10:02.0f}_{eta2x10:02.0f}/{name}_{sample}_a{alpha100:1.0f}",
                       {{"tierName", "Data"}, {"eta1x10", 0.0}, {"eta2x10", 13.0}, {"name", "mpfchs1"},
                        {"sample", "zjet"}, {"alpha100", 30.000000000000004}}) == "Data/eta_00_13/mpfchs1_zjet_a30");
  CHECK(formatTemplate("eta_{eta1x1001:02.0f}_{eta2x1001:02.0f}", {{"eta1x1001", 0.0}, {"eta2x1001", 13.013}}) ==
        "eta_00_13");
  CHECK(formatTemplate("eta{eta1:02.1f}-{eta2:02.1f}", {{"eta1", 0.0}, {"eta2", 1.3}}) == "eta0.0-1.3");
  CHECK(formatTemplate("eta{eta1:02d}-{eta2:02d}", {{"eta1", 0}, {"eta2", 13}}) == "eta00-13");
  CHECK(formatTemplate(";p_{{T}} (GeV);Response ({tier})", {{"tier", "ratio"}}) == ";p_{T} (GeV);Response (ratio)");
  CHECK(formatTemplate("{x}|{y}|{z}|{w}", {{"x", 1.0}, {"y", 0.1}, {"z", 1e16}, {"w", -7}}) == "1.0|0.1|1e+16|-7");
  CHECK(formatTemplate("{a:+.3e}|{b:g}|{c:08.3f}|{d:X}|{e: d}|{f:.0f}|{g:.0f}",
                       {{"a", 12345.678}, {"b", 1e-5}, {"c", -3.14159}, {"d", 255}, {"e", 42}, {"f", 0.5},
                        {"g", 2.5}}) == "+1.235e+04|1e-05|-003.142|FF| 42|0|2");
  const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(formatTemplate("{a:f}|{b:06.1f}|{c:F}|{d:+e}", {{"a", -nan}, {"b", inf}, {"c", -inf}, {"d", nan}}) ==
        "nan|000inf|-INF|+nan");
  CHECK(formatTemplate("{i:5.1f}", {{"i", 3}}) == "  3.0");  // an integer with a real spec
  CHECK(formatTemplate("no fields", {}) == "no fields");

  CHECK(throwsError([] { formatTemplate("{nope}", {{"yes", 1}}); }, "unknown key 'nope' (known: yes)"));
  CHECK(throwsError([] { formatTemplate("{x:02d}", {{"x", 1.5}}); }, "needs an integer"));
  CHECK(throwsError([] { formatTemplate("{x:5}", {{"x", 1.5}}); }, "unsupported format spec"));
  CHECK(throwsError([] { formatTemplate("{x:>5.1f}", {{"x", 1.5}}); }, "unsupported format spec"));
  CHECK(throwsError([] { formatTemplate("{x:5.1f}", {{"x", "text"}}); }, "on a text value"));
  CHECK(throwsError([] { formatTemplate("{x:#x}", {{"x", 0}}); }, "'#' not supported"));
  CHECK(throwsError([] { formatTemplate("{x:.2d}", {{"x", 1}}); }, "precision not allowed"));
  CHECK(throwsError([] { formatTemplate("a}b", {}); }, "single '}'"));
  CHECK(throwsError([] { formatTemplate("a{b", {}); }, "'{' without '}'"));
  CHECK(throwsError([] { formatTemplate("{0}", {}); }, "bad field name"));
  CHECK(throwsError([] { formatTemplate("{x!r}", {{"x", 1}}); }, "bad field name"));

  // Python's repr of a float.
  const std::pair<double, const char*> reprs[] = {
      {1.0, "1.0"}, {0.1, "0.1"}, {1e16, "1e+16"}, {1e15, "1000000000000000.0"}, {1e-4, "0.0001"},
      {1e-5, "1e-05"}, {1.5e-5, "1.5e-05"}, {-0.0, "-0.0"}, {123456789012345678.0, "1.2345678901234568e+17"},
      {0.30000000000000004, "0.30000000000000004"}, {5e-324, "5e-324"}, {1e100, "1e+100"}, {2.5, "2.5"},
      {-1234.5, "-1234.5"}, {1.7976931348623157e308, "1.7976931348623157e+308"}, {nan, "nan"}, {-inf, "-inf"}};
  for (const auto& [value, expected] : reprs) {
    if (pythonRepr(value) != expected) {
      std::cout << "  pythonRepr: got " << pythonRepr(value) << ", expected " << expected << "\n";
      CHECK(false);
    }
  }
}

void testOverrideKeys() {
  const ConfigFile good = configFrom(R"({
    "default": { "a": 1, "b": { "c": 2, "d": null }, "e": null, "_note": "x" },
    "eras":    { "UL16": { "b": { "c": 3, "d": { "anything": 1 } }, "_note": "y" } },
    "epochs":  { "2016GH": { "e": [1, 2], "a": null, "_n": 0 } } })");
  const nlohmann::json resolved = good.resolve("UL16", "2016GH");
  CHECK(!resolved.contains("a"));  // null in a merge patch removes the key
  CHECK(resolved["b"]["c"] == 3 && resolved["b"]["d"]["anything"] == 1);
  CHECK(resolved["e"].size() == 2 && !resolved.contains("_note"));

  // A typo in any era or epoch block is an error, also when another epoch is resolved.
  const ConfigFile typo = configFrom(R"({ "default": { "a": 1, "b": { "c": 2 } },
                                          "epochs": { "2018ABCD": { "b": { "cc": 3 } } } })");
  CHECK(throwsError([&] { typo.resolve("UL16", "2016GH"); },
                    "epochs.2018ABCD.b: unknown key 'cc' (not in \"default\")"));
  const ConfigFile typoTop = configFrom(R"({ "default": { "a": 1 }, "eras": { "UL17": { "aa": 1 } } })");
  CHECK(throwsError([&] { typoTop.resolve("UL17", "2017BCDEF"); }, "eras.UL17: unknown key 'aa'"));
  const ConfigFile notObject = configFrom(R"({ "default": { "a": 1 }, "eras": { "UL17": 3 } })");
  CHECK(throwsError([&] { notObject.resolve("UL17", "2017BCDEF"); }, "eras.UL17: expected a JSON object"));
}

void testInputs() {
  const Inputs inputs("config/Inputs.json");
  CHECK(inputs.root("prefit") == "input/prefit");
  CHECK(inputs.root("protolab") == "../JERCProtoLab");
  CHECK(inputs.resolvePath("{prefit}/jec/X.txt") == "input/prefit/jec/X.txt");
  CHECK(inputs.resolvePath("{protolab}/Summer19UL18/a.root") == "../JERCProtoLab/Summer19UL18/a.root");
  CHECK(inputs.resolvePath("plain/path.root") == "plain/path.root");
  CHECK(inputs.epoch("2016GH").forfit == "input/forfit/reprocess/jecdata2016GH.root");
  CHECK(inputs.epoch("2016GH").ptBinning.file == "input/prefit/DP2021033V2/drawDeltaJEC_16ULJECV5_vsBCDEF.root");
  CHECK(throwsError([&] { inputs.resolvePath("{nope}/x"); }, "unknown key 'nope'"));
  CHECK(throwsError([&] { inputs.root("nope"); }, "no root 'nope'"));
}

// Every config file with the default/eras/epochs layout resolves for every epoch,
// so no override in the repository has a key that is missing in "default".
void testAllConfigs() {
  const Inputs inputs("config/Inputs.json");
  int files = 0;
  for (const auto& entry : std::filesystem::directory_iterator("config")) {
    if (entry.path().extension() != ".json" || entry.path().filename() == "Inputs.json") continue;
    const ConfigFile config(entry.path().string());
    ++files;
    for (const std::string& name : inputs.epochNames()) {
      const Epoch& epoch = inputs.epoch(name);
      try {
        config.resolve(epoch.era, epoch.name);
      } catch (const JecSysError& error) {
        std::cout << "  " << error.what() << "\n";
        CHECK(false);
      }
    }
  }
  CHECK(files >= 2);  // JesModel9, MakeCard (the input creation tests input/config/ itself)
}

}  // namespace

int main() {
  std::filesystem::create_directories("obj/test");
  testFormatTemplate();
  testOverrideKeys();
  testInputs();
  testAllConfigs();
  return testTools::finish("testConfig");
}
