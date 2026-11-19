# JecSys, the fit stage (README.md). Builds one static library, obj/libjecsys.a, from
# cpp/ and one executable per file in bin/. The classes in header/ and cpp/ that are not
# specific to the fit (ConfigFile, Inputs, TextTools, CommandLine, InputFile, ...) are
# shared with the input-creation stage, which has its own Makefile in input/.
#
#   make            build the executables
#   make inputs     make the pre-fit files input/forfit/reprocess/jecdata<epoch>.root (see input/Makefile)
#   make test       unit tests + parity test against the golden outputs of the old chain
#   make clean
#
# Keep -O2 and add neither -ffast-math nor -march=native: FMA contractions change the
# last bits of results, and both stages must match their references byte for byte.

CXX      ?= g++
JSON_INC ?=   # directory that holds nlohmann/json.hpp, if it is not in a default path
CXXFLAGS := $(shell root-config --cflags) -O2 -g -Wall -Wextra -Wshadow -Iheader -Itest \
            $(if $(strip $(JSON_INC)),-isystem $(JSON_INC)) -MMD -MP
LDLIBS   := $(shell root-config --libs) -lMinuit

LIB_SRC  := $(wildcard cpp/*.cpp)
LIB_OBJ  := $(patsubst cpp/%.cpp,obj/%.o,$(LIB_SRC))
BIN_SRC  := $(wildcard bin/*.cpp)
BINS     := $(patsubst bin/%.cpp,%,$(BIN_SRC))
TEST_SRC := $(wildcard test/*.cpp)
TESTS    := $(patsubst test/%.cpp,obj/test/%,$(TEST_SRC))
LIBS     := obj/libjecsys.a

all: $(BINS)

obj/libjecsys.a: $(LIB_OBJ)
	@rm -f $@
	ar rcs $@ $^

obj/%.o: cpp/%.cpp
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/bin/%.o: bin/%.cpp
	@mkdir -p obj/bin
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BINS): %: obj/bin/%.o $(LIBS)
	$(CXX) $< $(LIBS) -o $@ $(LDLIBS)

obj/test/%: test/%.cpp $(LIBS)
	@mkdir -p obj/test
	$(CXX) $(CXXFLAGS) $< $(LIBS) -o $@ $(LDLIBS)

test: all $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; $$t || exit 1; done
	@bash test/parity.sh

inputs:
	$(MAKE) -C input

clean:
	rm -rf obj $(BINS)

.PHONY: all test clean inputs
-include $(LIB_OBJ:.o=.d) $(patsubst bin/%.cpp,obj/bin/%.d,$(BIN_SRC))
