#!/usr/bin/python3

# Instrument tests on the unitTest folder
# easier to do it here than in the buildbot script
# NOTE: passing in a list or arguments for dataflowProtection, we normally expect
#  it to start with a '-', however, argparse doesn't like that. Add an extra space
#  before your list of arguments

import os
import sys
import argparse
import subprocess
import shlex

singleFlag = False
verboseFlag = False

# class that represents a configuration
class runConfig(object):
    """docstring for runConfig."""
    def __init__(self, f, ef=None, xc=None, op=None, nm=None):
        self.fname = f
        self.extraFiles = ef
        self.xcFlg = xc
        self.optFlg = op
        self.noMemFlg = nm

# keep this up to date manually
# dictionary of specific flags for each unitTest
customConfigs = [
    runConfig("annotations.c"),
    runConfig("argSync.c", xc="-O3"),
    runConfig("atomics.c"),
    runConfig("basicIR.c"),
    runConfig("bsearch_strcmp.c"),
    runConfig("classTest.cpp"),
    runConfig("exceptions.cpp", \
        op="-replicateFnCalls=_ZNSt12_Vector_baseIiSaIiEE11_M_allocateEm,_ZSt27__uninitialized_default_n_aIPimiET_S1_T0_RSaIT1_E",  \
        nm="-ignoreFns=_ZNSt12_Vector_baseIiSaIiEE13_M_deallocateEPim"),
    runConfig("fSigTypes.c", \
        ef="fSigTypes_ext.c"),
    runConfig("helloWorld.cpp"),
    runConfig("inlining.c", \
        xc="-O2"),
    runConfig("load_store.c"),
    runConfig("mallocTest.c", \
        nm="-skipLibCalls=free"),
    runConfig("nestedCalls.c", \
        op="-replicateFnCalls=memset"),
    runConfig("ptrArith.c"),
    runConfig("returnPointer.c"),
    runConfig("segmenting.c"),
    runConfig("simd.c", \
        xc="-O3"),
    runConfig("structCompare.c"),
    runConfig("testFuncPtrs.c"),
    runConfig("time_c.c"),
    runConfig("vecTest.cpp", \
        op="-replicateFnCalls=_ZNSt12_Vector_baseIiSaIiEE11_M_allocateEm,_ZSt34__uninitialized_move_if_noexcept_aIPiS0_SaIiEET0_T_S3_S2_RT1_", \
        nm="-ignoreFns=_ZNSt12_Vector_baseIiSaIiEE13_M_deallocateEPim"),
    runConfig("verifyOptions.c"),
    runConfig("whetstone.c"),
    runConfig("zeroInit.c"),
]

def run(cfg, config, dir_path):
    # first clean before compiling
    clean = subprocess.Popen(['make', '-C', dir_path, 'small_clean'])
    clean.wait()
    # now build the test
    cmd = "make -C {} SRCFOLDER=./unitTests 'SRCFILES={}' 'XCFLAGS={}' 'OPT_PASSES={}'"
    fls = cfg.fname + " " + cfg.extraFiles if cfg.extraFiles else cfg.fname
    xcf = cfg.xcFlg if cfg.xcFlg else ""
    ps = config + " " + cfg.optFlg \
            if cfg.optFlg else config
    ps = ps + " " + cfg.noMemFlg \
            if (cfg.noMemFlg and "noMemReplication" in config) \
            else ps
    command = cmd.format(dir_path, fls, xcf, ps)
    if singleFlag or verboseFlag:
        print(command)
    p = subprocess.Popen(shlex.split(command))
    p.wait()
    # print(" --- return code: {}".format(p.returncode))
    return p.returncode


def main():
    global singleFlag, verboseFlag
    # CL arguments
    parser = argparse.ArgumentParser(description='Process commands for unit tests')
    parser.add_argument('config', help='configuration, without any file-specific flags')
    parser.add_argument('--single-run', '-s', help='Run only one file')
    parser.add_argument('--verbose', '-v', help='extra output', action='store_true')
    args = parser.parse_args()

    if args.verbose:
        verboseFlag = True

    # test dir
    dir_path = os.path.dirname(os.path.realpath(__file__))

    if args.single_run:
        single = [x for x in customConfigs if x.fname == args.single_run]
        returnVal = 0
        if len(single) > 0:
            singleFlag = True
            returnVal = run(single[0], args.config.lstrip(), dir_path)
        else:
            print("File name not found!")
        return returnVal
    else:
        for cfg in customConfigs:
            returnVal = run(cfg, args.config.lstrip(), dir_path)
            if returnVal != 0:
                if cfg.fname == "verifyOptions.c":
                    # this shouldn't be tested for success, because it's
                    #  supposed to fail
                    continue
                else:
                    return returnVal
    # clean one more time if we did all of them
    clean = subprocess.Popen(['make', '-C', dir_path, 'clean'])
    clean.wait()
    return returnVal


if __name__ == "__main__":
    returnVal = main()
    sys.exit(returnVal)
