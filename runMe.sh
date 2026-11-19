# Full chain for the four Run 2 UL reference epochs. Use: source runMe.sh
#
# 1. Input creation: input/prefit/ -> input/forfit/reprocess/jecdata<epoch>.root
#    (input/jecMakeInputs, built and run by input/Makefile for 2016GH, 2017BCDEF and
#    2018ABCD). input/forfit/reprocess/jecdata2016BCDEF.root is shipped;
#    "make -C input 2016BCDEF" remakes it.
# 2. Global fit: datacard (input/forfit/card/) -> workspace -> fit -> plots (output/postfit/),
#    for every epoch.
make -C input && make && ./runFit -e all
