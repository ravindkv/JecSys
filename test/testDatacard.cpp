// Unit tests of the datacard: parsing, writing, validation, look-up and combination.
#include "Datacard.h"
#include "TestTools.h"
#include "TextTools.h"

namespace {

const char* kCard = R"(# a comment
epoch     2016GH
corLevel  L1L2L3Res
etaBin    eta00-13
ptMin     15
-------------------------------
shapes data  *            shapes.root data/$CHANNEL
shapes *     *            shapes.root nuisance/$NUISANCE
shapes hadw* *            ../ref/shapes.root nuisance/$NUISANCE
aux    crecoil ptchs_multijet shapes.root aux/crecoil_$METHOD
aux    jesRef  *          shapes.root aux/jesRef
-------------------------------
channel   ptchs_multijet ptchs_gamjet ptchs_hadw chf_pfjet
kind      multijet       response     response   pfFraction
fraction  -              -            -          chf
delta     -              -            -          0.2
ptRange   -              -            -          43:600
-------------------------------
fsr_a        shape 1 - - -
scale_gamjet flat  - 0.002 - -   # source of the number
hdm_gamjet   flat  - 0.002 - -
hadw_fit     shape - - 1 -
inactive_x   shape - - - -
group fsr = fsr_a
)";

}  // namespace

int main() {
  using testTools::throwsError;

  // ---- parse ----
  Datacard card = Datacard::parse(kCard, "card/dir");
  card.validate();
  CHECK(card.epoch == "2016GH");
  CHECK(card.ptMin == 15);
  CHECK(card.channels.size() == 4);
  CHECK(card.channels[0].kind == ChannelKind::Multijet);
  CHECK(card.channels[3].kind == ChannelKind::PfFraction);
  CHECK(card.channels[3].fraction == "chf");
  CHECK(card.channels[3].delta && *card.channels[3].delta == 0.2);
  CHECK(card.channels[3].ptRange && card.channels[3].ptRange->second == 600);
  CHECK(card.nuisances.size() == 5);
  CHECK(card.nuisances[1].type == CardNuisance::Type::Flat);
  CHECK(card.nuisances[1].actsOn(1) && !card.nuisances[1].actsOn(0));
  CHECK(*card.nuisances[1].values[1] == 0.002);
  CHECK(card.nuisances[1].comment == "source of the number");
  CHECK(card.nuisances[4].isInactive());
  CHECK(card.groups.size() == 1 && card.groups[0].nuisances.size() == 1);
  CHECK(card.channelIndex("ptchs_hadw") == 2 && card.channelIndex("nope") == -1);

  // ---- look-up: substitution, base directory, last matching line wins ----
  CHECK(card.dataLocation("ptchs_gamjet")->objectPath == "data/ptchs_gamjet");
  CHECK(card.dataLocation("ptchs_gamjet")->file == "card/dir/shapes.root");
  CHECK(card.nuisanceLocation("fsr_a", "ptchs_multijet")->objectPath == "nuisance/fsr_a");
  CHECK(card.nuisanceLocation("hadw_fit", "ptchs_hadw")->file == "card/ref/shapes.root");
  CHECK(card.auxLocation("crecoil", "ptchs_multijet")->objectPath == "aux/crecoil_ptchs");
  CHECK(!card.auxLocation("crecoil", "ptchs_gamjet"));
  CHECK(Datacard::methodOf("mpfchs1_zjet") == "mpfchs1" && Datacard::sampleOf("mpfchs1_zjet") == "zjet");

  // ---- write and read back: same content ----
  const Datacard again = Datacard::parse(card.toText(), "card/dir");
  again.validate();
  CHECK(again.toText() == card.toText());
  CHECK(again.nuisances[1].comment == card.nuisances[1].comment);

  // ---- warnings: inactive and identical nuisances ----
  const auto warnings = card.warnings();
  CHECK(warnings.size() == 2);
  CHECK(textTools::join(warnings, "|").find("'scale_gamjet' and 'hdm_gamjet' are identical") != std::string::npos);
  CHECK(textTools::join(warnings, "|").find("'inactive_x' acts on no channel") != std::string::npos);

  // ---- errors are caught, with a useful message ----
  const auto broken = [](const std::string& from, const std::string& to) {
    return [=]() { Datacard::parse(textTools::replaceAll(kCard, from, to), ".").validate(); };
  };
  CHECK(throwsError(broken("fsr_a        shape 1 - - -", "fsr_a        shape 1 - -"), "columns"));
  CHECK(throwsError(broken("kind      multijet", "kind      multijets"), "Unknown channel kind"));
  CHECK(throwsError(broken("scale_gamjet flat", "scale_gamjet lnN"), "unknown keyword"));
  CHECK(throwsError(broken("- 0.002 - -   #", "- abc - -   #"), "Expected a number"));
  CHECK(throwsError(broken("delta     -              -            -          0.2", ""), "needs 'fraction', 'delta'"));
  CHECK(throwsError(broken("aux    crecoil ptchs_multijet shapes.root aux/crecoil_$METHOD", ""), "aux crecoil"));
  CHECK(throwsError(broken("shapes data  *            shapes.root data/$CHANNEL", ""), "no 'shapes data' line"));
  CHECK(throwsError(broken("group fsr = fsr_a", "group fsr = fsr_b"), "unknown nuisance"));
  CHECK(throwsError(broken("hdm_gamjet   flat", "scale_gamjet flat"), "listed twice"));
  CHECK(throwsError(broken("hadw_fit     shape - - 1 -", "hadw_fit     shape - - - 1"), "pfFraction channel"));
  CHECK(throwsError(broken("ptMin     15", "ptMin     15\nptMin 20"), "given twice"));

  // ---- combination ----
  Datacard other = card;
  other.epoch = "2016BCDEF";
  other.setBaseDir("card/other");
  const Datacard combined = Datacard::combine({{"A", card}, {"B", other}}, "card/out", "fsr_.*");
  CHECK(combined.channels.size() == 8);
  CHECK(combined.channels[4].name == "B_ptchs_multijet");
  CHECK(combined.epoch == "2016GH+2016BCDEF");
  // correlated by name: one scale_gamjet acting on both cards
  const int scale = combined.nuisanceIndex("scale_gamjet");
  CHECK(scale >= 0 && combined.nuisances[scale].actsOn(1) && combined.nuisances[scale].actsOn(5));
  // decorrelated: two FSR nuisances, each on its own card, reading the original histogram
  const int fsrA = combined.nuisanceIndex("fsr_a_A"), fsrB = combined.nuisanceIndex("fsr_a_B");
  CHECK(fsrA >= 0 && fsrB >= 0 && combined.nuisanceIndex("fsr_a") < 0);
  CHECK(combined.nuisances[fsrA].actsOn(0) && !combined.nuisances[fsrA].actsOn(4));
  CHECK(combined.nuisances[fsrB].actsOn(4) && !combined.nuisances[fsrB].actsOn(0));
  CHECK(combined.nuisanceLocation("fsr_a_B", "B_ptchs_multijet")->objectPath == "nuisance/fsr_a");
  CHECK(combined.nuisanceLocation("fsr_a_B", "B_ptchs_multijet")->file == "card/other/shapes.root");
  CHECK(combined.dataLocation("A_ptchs_gamjet")->objectPath == "data/ptchs_gamjet");
  CHECK(combined.auxLocation("crecoil", "B_ptchs_multijet")->objectPath == "aux/crecoil_ptchs");
  CHECK(combined.groups[0].nuisances.size() == 2);
  CHECK(throwsError([&]() { Datacard::combine({{"A", card}, {"A", other}}, "."); }, "used twice"));

  return testTools::finish("testDatacard");
}
