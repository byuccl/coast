LEVEL = ../..
TARGET = sha256_tmr
CFLAGS = -O3 -fno-slp-vectorize -g
OPT_PASSES = -O3 -disable-slp-vectorization -TMR -verbose -countErrors
OPT_FLAGS =

BOARD = hiFive1
OOCD_GDB_PORT ?= 3333

include $(LEVEL)/makefiles/Makefile.common
