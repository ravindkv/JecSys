// Unit tests of the settings of the input creation: every input/config/*.json resolves
// for every epoch of config/Inputs.json and parses into the classes' typed settings, and
// bad settings are reported with a clear message. Run from the repository root.
#include <filesystem>

#include "ConfigFile.h"
#include "InputChain.h"
#include "Inputs.h"
#include "Reprocess.h"
#include "TAttMarker.h"
#include "SoftRad.h"
#include "TdrStyle.h"
#include "TestTools.h"

namespace {

using testTools::throwsError;

void testAllConfigs() {
  const Inputs inputs("config/Inputs.json");
  int files = 0;
  for (const auto& entry : std::filesystem::directory_iterator("input/config")) {
    if (entry.path().extension() != ".json") continue;
    ++files;
    const ConfigFile config(entry.path().string());
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
  CHECK(files == 6);

  // Every step parses its settings in its constructor (with TdrStyle.json).
  for (const std::string& name : inputs.epochNames()) {
    try {
      const InputChain chain(inputs, inputs.epoch(name), InputChain::Options());
    } catch (const JecSysError& error) {
      std::cout << "  " << name << ": " << error.what() << "\n";
      CHECK(false);
    }
  }
}

void testChain() {
  const Inputs inputs("config/Inputs.json");
  const Epoch& epoch = inputs.epoch("2016GH");
  InputChain::Options options;
  options.steps = {"softrad3"};
  CHECK(throwsError([&] { InputChain(inputs, epoch, options); }, "unknown step 'softrad3'"));
  options.steps = {"reprocess"};
  options.jecInversion = "bisect";
  CHECK(throwsError([&] { InputChain(inputs, epoch, options); }, "must be getx, memo or secant"));
}

void testReprocessSettings() {
  const Inputs inputs("config/Inputs.json");
  for (const std::string& name : inputs.epochNames()) {
    const Reprocess reprocess(inputs, inputs.epoch(name));
    const ReprocessSettings& s = reprocess.settings();
    CHECK(s.inversion == "memo");
    CHECK(s.inputs.zjet.rfind("input/prefit/", 0) == 0);  // {prefit} resolved
    CHECK(s.correctors.size() == 26 && !s.combined.empty());
  }
  // A null left for an era is an error naming the epoch.
  const ReprocessSettings parsed = ReprocessSettings::parse(
      ConfigFile("input/config/Reprocess.json").resolve("UL17", "2017BCDEF"), "test", inputs);
  CHECK(parsed.marker.at("zjet").at("chf") == kFullCircle && parsed.color.at("zjet") == kRed + 1);
  CHECK(throwsError(
      [&] {
        ReprocessSettings::parse(ConfigFile("input/config/Reprocess.json").resolve("none", "none"), "test", inputs);
      },
      "is not set for this epoch (null)"));
  CHECK(inputTools::rootConstant("kGreen+2", "test") == kGreen + 2);
  CHECK(throwsError([] { inputTools::rootConstant("kPurple", "test"); }, "unknown ROOT constant 'kPurple'"));
}

void testTdrStyle() {
  const auto style = [](const char* setters) {
    return TdrStyle(nlohmann::json::parse(std::string(R"({"name": "s", "title": "t", "setters": )") + setters + "}"),
                    "test");
  };
  style(R"([["SetOptStat", 0], ["SetTitleSize", 0.06, "XYZ"], ["SetTitleFont", 42], ["SetPaperSize", 20.0, 20]])");
  CHECK(throwsError([&] { style(R"([["SetNoSuchThing", 1]])"); }, "unknown TStyle setter"));
  CHECK(throwsError([&] { style(R"([["SetOptStat", 0.5]])"); }, "has the wrong type"));
  CHECK(throwsError([&] { style(R"([["SetFitFormat", 5]])"); }, "has the wrong type"));
  CHECK(throwsError([&] { style(R"([["SetPaperSize", 20.0]])"); }, "expects 2 argument(s)"));
  CHECK(throwsError([&] { style(R"([["SetTitleSize", 0.06, "XYZ", 1]])"); }, "expects 1 or 2 arguments"));
}

void testExpandSamples() {
  std::set<std::string> mcOnly;
  const auto samples = SoftRad::expandSamples({"zjet", "z*", "z**"}, "ib", mcOnly);
  const std::vector<std::string> expected = {"zjet", "zi", "zb", "zii", "zib", "zbi", "zbb"};
  CHECK(samples == expected);
  CHECK(mcOnly == std::set<std::string>({"zii", "zib", "zbi", "zbb"}));
}

}  // namespace

int main() {
  testAllConfigs();
  testChain();
  testReprocessSettings();
  testTdrStyle();
  testExpandSamples();
  return testTools::finish("testInputSettings");
}
