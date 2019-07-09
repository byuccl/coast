rm output.opt.bc
make OPT_PASSES="-DWC" OPT_FLAGS="" SRCFILES="helloWorld.cpp" SRCFOLDER=./unitTests
