# The input-creation stage: makes the pre-fit files forfit/reprocess/jecdata<epoch>.root
# from the external inputs in prefit/ (and ../../JERCProtoLab) with input/jecMakeInputs
# (README.md). It is built separately from the fit stage (../Makefile), but shares its
# common classes (../header, ../cpp: ConfigFile, Inputs, TextTools, ...) and
# ../config/Inputs.json. Run it as make -C input (or make inputs from the top level).
#
#   make                 build, then make the pre-fit files of the epochs in EPOCHS
#   make 2016GH          one epoch (make -j runs several epochs in parallel)
#   make 2016BCDEF       the reference IOV of 2016 (its file is shipped; not in EPOCHS)
#   make lib             only build the libraries (obj/libjecinput.a, obj/libcondformats.a)
#   make bin             ... and input/jecMakeInputs (run it from the repository root)
#   make test            unit tests (test/*.cpp) and the byte-parity test (test/parity.sh)
#   make clean           remove the build products (obj/, jecMakeInputs), not the jecdata files
#
# Each epoch runs the chain of config/InputChain.json: reprocess -> softRad (x2) ->
# globalFitSyst -> globalFitRenormPF. Keep -O2 and add neither -ffast-math nor
# -march=native: FMA contractions would change the last bits, and the output is checked
# byte for byte against the goldens made by the ROOT macros the classes replaced.

EPOCHS     ?= 2016GH 2017BCDEF 2018ABCD
ALL_EPOCHS := 2016BCDEF 2016GH 2017BCDEF 2018ABCD

all: $(EPOCHS)

# ---------------------------------------------------------------- C++ (run as make -C input)

CXX         ?= g++
JSON_INC    ?=   # directory that holds nlohmann/json.hpp, if it is not in a default path
ROOT_CFLAGS := $(shell root-config --cflags)
CXXFLAGS    := $(ROOT_CFLAGS) -O2 -g -Wall -Wextra -Wshadow -Iheader -I../header \
               $(if $(strip $(JSON_INC)),-isystem $(JSON_INC)) -MMD -MP

LDLIBS      := $(shell root-config --libs)

# The input classes, cpp/*.cpp -> obj/libjecinput.a. Executables (bin/*.cpp, built here
# in input/) and tests link it with the JEC classes and the shared classes of the top level.
LIB_SRC  := $(wildcard cpp/*.cpp)
LIB_OBJ  := $(patsubst cpp/%.cpp,obj/%.o,$(LIB_SRC))
BIN_SRC  := $(wildcard bin/*.cpp)
BINS     := $(patsubst bin/%.cpp,%,$(BIN_SRC))
TEST_SRC := $(wildcard test/*.cpp)
TESTS    := $(patsubst test/%.cpp,obj/test/%,$(TEST_SRC))
SHARED   := ../obj/libjecsys.a

# The CMS JEC classes (third-party, not edited), with their package layout: headers in
# header/CondFormats/JetMETObjects/interface/, sources in cpp/CondFormats/JetMETObjects/src/.
# Utilities.cc is left out: JetCorrectorParameters.cc #includes it (hence -Icpp).
# header/prelude.h supplies the "using namespace std" that two of the files rely on;
# -w silences their warnings.
CF_SRC_DIR := cpp/CondFormats/JetMETObjects/src
CF_SRC     := $(filter-out %/Utilities.cc,$(wildcard $(CF_SRC_DIR)/*.cc))
CF_OBJ     := $(patsubst $(CF_SRC_DIR)/%.cc,obj/cf/%.o,$(CF_SRC))
CF_FLAGS   := $(ROOT_CFLAGS) -O2 -g -Iheader -Icpp -include header/prelude.h -w -MMD -MP

LIBS := $(if $(LIB_OBJ),obj/libjecinput.a) obj/libcondformats.a

lib: $(LIBS)

obj/libjecinput.a: $(LIB_OBJ)
	@rm -f $@
	ar rcs $@ $^

obj/libcondformats.a: $(CF_OBJ)
	@rm -f $@
	ar rcs $@ $^

obj/%.o: cpp/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/cf/%.o: $(CF_SRC_DIR)/%.cc
	@mkdir -p obj/cf
	$(CXX) $(CF_FLAGS) -c $< -o $@

# The shared classes are built by the top-level Makefile.
$(SHARED): FORCE
	@$(MAKE) --no-print-directory -C .. obj/libjecsys.a
FORCE:

bin: $(BINS)

obj/bin/%.o: bin/%.cpp
	@mkdir -p obj/bin
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BINS): %: obj/bin/%.o $(LIBS) $(SHARED)
	$(CXX) $< $(LIBS) $(SHARED) -o $@ $(LDLIBS)

obj/test/%: test/%.cpp $(LIBS) $(SHARED)
	@mkdir -p obj/test
	$(CXX) $(CXXFLAGS) -I../test $< $(LIBS) $(SHARED) -o $@ $(LDLIBS)

# Run from the repository root, like the executables; the parity test uses ../jecCompare.
test: bin $(TESTS)
	@$(MAKE) --no-print-directory -C .. jecCompare
	@cd .. && for t in $(TESTS); do echo "== input/$$t"; input/$$t || exit 1; done
	@bash test/parity.sh

# ---------------------------------------------------------------- the pre-fit files

$(ALL_EPOCHS): bin
	cd .. && input/jecMakeInputs -e $@

clean:
	rm -rf obj $(BINS)

.PHONY: all lib bin test clean FORCE $(ALL_EPOCHS)
-include $(LIB_OBJ:.o=.d) $(CF_OBJ:.o=.d) $(patsubst bin/%.cpp,obj/bin/%.d,$(BIN_SRC))
