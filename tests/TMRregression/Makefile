######## 	   Change the following commands		########
XCFLAGS ?=
XLFLAGS ?= -lm
OPT_FLAGS ?=
OPT_PASSES ?=
SRCFILES ?=
SRCFOLDER ?=
TARGET ?= output
EXECARGS ?=
CSTD ?= c11
CPPSTD ?= c++11
CPPFLAGS := -stdlib=libc++ -I/usr/include/c++/5.4.0/ -I/usr/include/x86_64-linux-gnu/c++/5.4.0/
###########################################################
# The above flags should be reset by the command line:
# XCFLAGS - extra flags to pass into clang
# XLFLAGS - extra flags to be used by the linker
# OPT_FLAGS - the flags passed into the llvm `opt` command
# OPT_PASSES - the custom passes to be run by opt
# SRCFILES - a list of all the source files, space-separated
# SRCFOLDER - path to the folder to find the SRCFILES
# TARGET - name of the target executable
# EXECARGS - arguments to use when executing
# CSTD - C standard to use when compiling files, default is c11
# CPPSTD - C++ standard to use when compiling files, default is c++11
###########################################################

CLANG = clang-7
CLANG++ = clang++-7
LLVMDIS = llvm-dis-7
LLVMOPT = opt-7
LLVMLLC	= llc-7
LLVMLINK= llvm-link-7

export COAST_ROOT = $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/../../

CFLAGS = -I$(SRCFOLDER)

## Recursive wildcard
rwildcard=$(wildcard $1$2)$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
PROJECT_BUILD_DIR = $(COAST_ROOT)/projects/build/
PROJECT_LIBS := $(call rwildcard,$(PROJECT_BUILD_DIR),*.so)
OPT_LIBS_LOAD = -load $(COAST_ROOT)/projects/build/errorBlocks/ErrorBlocks.so -load $(COAST_ROOT)/projects/build/dataflowProtection/DataflowProtection.so $(foreach lib, $(PROJECT_LIBS) , -load $(lib))

CFILES = $(filter %.c,$(SRCFILES))
CPPFILES = $(filter %.cpp,$(SRCFILES))
# it is possible to manually specify the .bc files to use,
#  but this is not the common case
ifndef $(BCFILES)
BCFILES = $(CFILES:.c=.bc)
endif
ifndef $(BCPPFILES)
BCPPFILES = $(CPPFILES:.cpp=.bcpp)
endif

# ANSI escape color codes
COLOR_BLUE			:= '\033[0;34m'
COLOR_MAGENTA		:= '\033[0;35m'
COLOR_YELLOW		:= '\033[0;33m'
COLOR_GREEN			:= '\033[0;32m'
COLOR_BLACK			:= '\033[0;30m'
NO_COLOR			:= '\033[0m'
HIGHLIGHT			:= '\033[0;7m'

.PHONY: debug

run: $(TARGET)
	./$(TARGET) $(EXECARGS)

compile: $(TARGET)

test: $(TARGET).s

# used to verify correct Macro definition
debug:
	@echo SRCFILES= $(SRCFILES)
	@echo CFILES= $(CFILES)
	@echo CPPFILES= $(CPPFILES)
	@echo BCFILES= $(BCFILES)
	@echo BCPPFILES = $(BCPPFILES)

$(TARGET): $(TARGET).s
ifeq ($(CPPFILES),)
	$(CLANG) $(TARGET).s $(XLFLAGS) -o $@
else
	$(CLANG++) -stdlib=libc++ $(TARGET).s $(XLFLAGS) -o $@
endif

# rule for .s files
$(TARGET).s: $(TARGET).opt.bc
	$(LLVMLLC) -asm-verbose $< -o=$@

# rule for opt.bc files
$(TARGET).opt.bc: $(TARGET).clang.bc
	@$(LLVMDIS) -f $<
	$(LLVMOPT) $(OPT_FLAGS) $(OPT_LIBS_LOAD) $(OPT_PASSES) -o $@ $<
	@$(LLVMDIS) -f $@

$(TARGET).clang.bc: $(BCFILES) $(BCPPFILES)
	@$(LLVMLINK) $(BCFILES) $(BCPPFILES) -o $(TARGET).clang.bc

# rule for .bc files
%.bc: $(SRCFOLDER)/%.c
	$(CLANG) -std=$(CSTD) -emit-llvm $(CFLAGS) $(XCFLAGS) $< -c

%.bcpp: $(SRCFOLDER)/%.cpp
	$(CLANG++) -std=$(CPPSTD) -emit-llvm $(CPPFLAGS) $< -c -o $@

.PHONY: clean small_clean

small_clean:
	@rm -f *.bc *.bcpp *.s $(TARGET)

clean: small_clean
	@rm -f *.ll

cfg: $(TARGET).opt.ll $(TARGET).clang.ll
	@rm -rf cfg
	@mkdir cfg
	$(LLVMOPT) --dot-cfg $(TARGET).clang.ll > /dev/null
	$(eval DOT_FILES = *.dot)
	for file in $(DOT_FILES); do \
		dot $$file -Tpdf > ./cfg/$$file.clang.pdf ;\
	done
	$(LLVMOPT) --dot-cfg $(TARGET).opt.ll > /dev/null
	for file in $(DOT_FILES); do \
		dot $$file -Tpdf > ./cfg/$$file.opt.pdf ;\
	done
	rm -rf *.dot
