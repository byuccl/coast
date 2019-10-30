rm output.opt.bc
make OPT_PASSES="-DWC" OPT_FLAGS="" SRCFILES="load_store.c" SRCFOLDER=./unitTests
