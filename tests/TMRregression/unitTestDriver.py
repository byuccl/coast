#!/usr/bin/python3

# Instrument tests on the unitTest folder
# easier to do it here than in the buildbot script
# NOTE: passing in a list or arguments for dataflowProtection, we normally expect
#  it to start with a '-', however, argparse doesn't like that. Add an extra space
#  before your list of arguments

import os
import re
import sys
import time
import shlex
import signal
import pathlib
import argparse
import subprocess


# globals
singleFlag = False
verboseFlag = False
this_dir = pathlib.Path(__file__).resolve().parents[0]
coast_root = this_dir.parents[1]
makefile_path = coast_root / "unittest" / "makefile.customFile"
code_path = this_dir / "unitTests"
user_incs = code_path.parents[1]
qemuWaitTime = 2

# discover GCC version
gcc_proc = subprocess.Popen(['gcc', '--version'], stdout=subprocess.PIPE)
gcc_output = gcc_proc.communicate()[0].decode()
gcc_version_re = re.compile(r"gcc \((.*)\) (\d\.\d\.\d)")
gcc_match = gcc_version_re.search(gcc_output)
gcc_version_num = gcc_match.group(2)

# long regexes
ptrArithRegex = re.compile(r"""\
 1\s+2\s+0\s+4\s+0\s+-1\s+7\s+5\s+
 0xFA, 0x55
 2\s+2\s+7\s+4\s+5\s+6\s+7\s+8\s+
 3
 1 @ 0x[A-Fa-f0-9]+
 2 @ 0x[A-Fa-f0-9]+
 3 @ 0x[A-Fa-f0-9]+
 4 @ 0x[A-Fa-f0-9]+
0x[A-Fa-f0-9]+, 0x[A-Fa-f0-9]+
 1 @ 0x[A-Fa-f0-9]+
 8 @ 0x[A-Fa-f0-9]+
42 @ 0x[A-Fa-f0-9]+
 4 @ 0x[A-Fa-f0-9]+
 0x00AB
 4\s+7\s+16\s+32\s+
 [579]
Success!""", re.MULTILINE)
# TODO: last integer
timeCRegex = re.compile(r"""\
Using time and ctime: [A-z]{3,5} [A-z]{3}\s+(0?[1-9]|[12]\d|3[01]) ([01]\d|2[0-3]):?([0-5]\d):?([0-5]\d) \d+
Using localtime and asctime: [A-z]{3,5} [A-z]{3}\s+(0?[1-9]|[12]\d|3[01]) ([01]\d|2[0-3]):?([0-5]\d):?([0-5]\d) \d+
Using difftime and mktime: \d+\.\d+ seconds since today started
Using gmtime and strftime: GMT - ([01]\d|2[0-3]):?([0-5]\d)
Using clock: \d+ clicks to run \(\d\.\d+ seconds\)
""")
whetstoneRegex = re.compile(r"""\
Loops: [1-9][0-9]+, Iterations: [1-9][0-9]*, Duration: [0-9]+ sec.
C Converted Double Precision Whetstones: [0-9]+\.[0-9]+ MIPS
""")


# class that represents a configuration
class runConfig(object):
    """docstring for runConfig."""
    def __init__(self, f, ef=None, xc=None, op=None, nm=None, cf=False, hk=False, sn=False, xl=None, xlc=None, qtm=None, rgx=None, brd=None):
        self.fname = f
        self.extraFiles = ef    # other files to use in compilation
        self.xcFlg = xc         # additional flags in clang compile step
        self.optFlg = op        # additional flags in optimizer step
        self.xlFlg = xl         # additional flags in link step
        self.xlcFlg = xlc       # additional flags in assembler step
        self.noMemFlg = nm      # flags to add to opt when "noMemReplication" is used
        self.compileFail = cf   # supposed to fail in compile
        self.hardKill = hk      # must be killed by ctrl-c
        self.skipNormal = sn    # don't run without COAST
        self.qemuTime = qtm     # how long to wait before terminating QEMU
        self.outRegx = rgx      # regex for validating output printing
        self.board = brd        # specify default test target

# keep this up to date manually
# dictionary of specific flags for each unitTest
customConfigs = [
    runConfig("annotations.c"),
    runConfig("argAttrs.c"),
    runConfig("argSync.c", xc="-O3"),
    runConfig("arm_locks.c", brd="pynq", hk=True),
    runConfig("atomics.c", nm="__SKIP_THIS",
        rgx=re.compile(r"counter = [2-4]")),
    runConfig("basicIR.c"),
    runConfig("bsearch_strcmp.c"),
    runConfig("classTest.cpp"),
    runConfig("cloneAfterCall.c", sn=True,
        rgx=re.compile(r"Bob \(16\): 3.7[0-9]*\nSuccess!\n", re.MULTILINE)),
    runConfig("exceptions.cpp", \
        op="-replicateFnCalls=_ZNSt12_Vector_baseIiSaIiEE11_M_allocateEm,_ZSt27__uninitialized_default_n_aIPimiET_S1_T0_RSaIT1_E",  \
        nm="-ignoreFns=_ZNSt12_Vector_baseIiSaIiEE13_M_deallocateEPim"),
    runConfig("fibonacci.c", sn=True),
    runConfig("fSigTypes.c", \
        ef="fSigTypes_ext.c"),
    runConfig("funcPtrStruct.c",
        rgx=re.compile(r"100 150\n(1 2 3\n){1,3}Finished", re.MULTILINE)),
    runConfig("globalPointers.c", \
        xc="-g3", cf=True, sn=True),
    runConfig("halfProtected.c", op="-skipLibCalls=malloc"),
    runConfig("helloWorld.cpp"),
    runConfig("inlining.c", \
        xc="-O2"),
    runConfig("linkedList.c", xc="-g3", cf=True, sn=True),
    runConfig("load_store.c"),
    runConfig("mallocTest.c", sn=True,
        rgx=re.compile(r"^Finished", re.MULTILINE)),
    runConfig("nestedCalls.c", xc="-O2",\
        op="-replicateFnCalls=memset"),
    runConfig("ptrArith.c", rgx=ptrArithRegex),
    runConfig("protectedLib.c", op="-protectedLibFn=sharedFunc"),
    runConfig("replReturn.c", sn=True, nm="__SKIP_THIS",
        op="-cloneReturn=returnTest -replicateFnCalls=malloc -cloneFns=testWrapper",
        rgx=re.compile(r"(0x[0-9A-Fa-f]+\n){2,3}Success!\n", re.MULTILINE)),
    runConfig("returnPointer.c"),
    runConfig("segmenting.c"),
    runConfig("signalHandlers.c", hk=True,
        op="-skipLibCalls=__sysv_signal,signal"),
    runConfig("simd.c", \
        xc="-O3"),
    runConfig("stackAttack.c", xc="-g3"),
    runConfig("stackProtect.c", qtm=1, xc="-g3", op="-protectStack"),
    runConfig("structCompare.c"),
    runConfig("testFuncPtrs.c"),
    runConfig("time_c.c", op="-skipLibCalls=clock -cloneAfterCall=time",
        rgx=timeCRegex),
# The Travis Docker has GCC v7.5.0, Ubuntu 18.04. vecTest.cpp was tested on GCC v5.4.0, Ubuntu 16.04.
# Between compiler versions, there were apparently significant changes to how vectors work,
#  and these flags actually now break the test instead of fixing it.
# Add them in when on the old system.
    runConfig("vecTest.cpp",
        op="-replicateFnCalls=_ZNSt12_Vector_baseIiSaIiEE11_M_allocateEm,_ZSt34__uninitialized_move_if_noexcept_aIPiS0_SaIiEET0_T_S3_S2_RT1_" if gcc_version_num == "5.4.0" else None,
        nm="-ignoreFns=_ZNSt12_Vector_baseIiSaIiEE13_M_deallocateEPim" if gcc_version_num == "5.4.0" else None),
    runConfig("verifyOptions.c", cf=True, sn=True),
    runConfig("whetstone.c", xl="-lm", rgx=whetstoneRegex),
    runConfig("zeroInit.c"),
]


def run(cfg, config, dir_path, board=None, no_clean=False):
    """run a single test with the given configuration.

    cfg - a runConfig object
    config - the COAST command line configuration
    dir_path - path to the test to run
    board - argument to the Make variable `BOARD`
    no_clean - request that the files not be removed,
            even if run is successful
    """
    # skip if no COAST applied
    if cfg.skipNormal and (not config):
        return 0

    # corner case skip
    if ("noMemReplication" in config) and (cfg.noMemFlg == "__SKIP_THIS"):
        return 0

    if (board is None) and (cfg.board is not None):
        board = cfg.board

    # first clean before compiling
    target_name = os.path.splitext(cfg.fname)[0]
    clean_cmd = "make --no-print-directory --file={mkfl} -C {dir} TARGET={tgt} clean".format(
        mkfl=makefile_path,
        dir=dir_path,
        tgt=target_name
    )
    if board is not None:
        clean_cmd += " 'BOARD={}'".format(board)
    clean = subprocess.Popen(shlex.split(clean_cmd))
    clean.wait()

    # now build the test
    cmd = "make --no-print-directory --file={mkfl} -C {srcdir} 'PROJECT_SRC={codedir}' '{flspec}={srcfiles}' 'USER_CFLAGS={xcfl}' '{inc_name}={incs}' 'XLFLAGS={xlfl}' 'XLLCFLAGS={xllc}' 'OPT_PASSES={opt}' 'TARGET={tgt}'"
    fls = cfg.fname + " " + cfg.extraFiles if cfg.extraFiles else cfg.fname
    xcf = cfg.xcFlg if cfg.xcFlg else ""
    xlf = cfg.xlFlg if cfg.xlFlg else ""
    xllc = cfg.xlcFlg if cfg.xlcFlg else ""
    ps = config + " " + cfg.optFlg \
            if cfg.optFlg else config
    ps = ps + " " + cfg.noMemFlg \
            if (cfg.noMemFlg and "noMemReplication" in config) \
            else ps

    # default is x86 for automated testing, but support other boards
    #  for manual testing purposes
    if board == "x86":
        fileSpecName = "SRCFILES"
        runProgName = "program"
        incName = "USER_INCS"
    elif board == "pynq":
        fileSpecName = "CSRCS"
        runProgName = "qemu"
        incName = "INC_DIRS"
    elif board == "ultra96":
        fileSpecName = "CSRCS"
        runProgName = "program"
        incName = "USER_INCS"
        # TODO: cool to get QEMU working with this target too
    else:
        # default - TODO: check correctness
        fileSpecName = "SRCFILES"
        runProgName = "program"
        incName = "USER_INCS"

    command = cmd.format(
        mkfl=makefile_path,
        srcdir=dir_path,
        codedir=code_path,
        flspec=fileSpecName,
        srcfiles=fls,
        xcfl=xcf,
        inc_name=incName,
        incs=user_incs,
        xlfl=xlf,
        xllc=xllc,
        opt=ps,
        tgt=target_name
    )
    if board is not None:
        command += " 'BOARD={}'".format(board)
    if board == 'pynq':
        command += " 'BUILD_FOR_SIMULATOR=1'"
    if singleFlag or verboseFlag:
        print(command)
    try:
        p = subprocess.Popen(shlex.split(command + " exe"))
        p.wait()
    except KeyboardInterrupt as ki:
        if cfg.hardKill:
            # success
            return 0
        else:
            raise ki
    # other exceptions are not handled
    except Exception as e:
        raise e
    # print(" --- return code: {}".format(p.returncode))

    if cfg.compileFail:
        # this shouldn't be tested for success, because it's
        #  supposed to fail
        print(" (Expected compilation failure)")
        return not p.returncode
    elif p.returncode:
        return p.returncode

    # now run it
    try:
        runCmd = command + " {}".format(runProgName)
        # QEMU must be killed here, because it otherwise will hang the whole test
        if board == 'pynq':
            # https://stackoverflow.com/a/4791612/12940429
            p = subprocess.Popen(shlex.split(runCmd), preexec_fn=os.setsid,
                    stdout=subprocess.PIPE)
            # wait for process to run
            if cfg.qemuTime is not None:
                # allow override sleep time
                time.sleep(cfg.qemuTime)
            else:
                # these are short running things
                time.sleep(qemuWaitTime)
            # sends signal to whole process group
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
            # fix the terminal echo
            os.system("stty echo")
        else:
            p = subprocess.Popen(shlex.split(runCmd),
                    stdout=subprocess.PIPE)
        p.wait()
        output, errorMsg = p.communicate()
        output = output.decode()
        print(output.rstrip())
    except KeyboardInterrupt as ki:
        if cfg.hardKill:
            return 0
        else:
            raise ki

    # return value: assume fail unless otherwise noted
    returnVal = None

    # some may have output regexes
    if cfg.outRegx is not None:
        outputMatch = re.search(cfg.outRegx, output)
        if outputMatch:
            # success
            returnVal = 0
        else:
            print("Didn't match!")
            returnVal = -1

    # now check return code
    if p.returncode:
        returnVal = p.returncode
    elif (returnVal is None) and (p.returncode == 0):
        # success
        returnVal = 0

    # clean at end also, if succeeded
    if (not returnVal) and (not no_clean):
        clean2 = subprocess.Popen(shlex.split(clean_cmd))
        clean2.wait()
    # now we're done
    return returnVal


def addMoreFlags(runCfg, args):
    if args.extra_clang_flags:
        if runCfg.xcFlg is None:
            runCfg.xcFlg = args.extra_clang_flags
        else:
            runCfg.xcFlg += args.extra_clang_flags
    if args.extra_opt_flags:
        if runCfg.optFlg is None:
            runCfg.optFlg = args.extra_opt_flags
        else:
            runCfg.optFlg += args.extra_opt_flags
    if args.extra_link_flags:
        if runCfg.xlFlg is None:
            runCfg.xlFlg = args.extra_link_flags
        else:
            runCfg.xlFlg += args.extra_link_flags
    if args.extra_lc_flags:
        if runCfg.xlcFlg is None:
            runCfg.xlcFlg = args.extra_lc_flags
        else:
            runCfg.xlcFlg += args.extra_lc_flags
    return runCfg


def main():
    global singleFlag, verboseFlag
    # CL arguments
    parser = argparse.ArgumentParser(description='Process commands for unit tests')
    parser.add_argument('config', help='configuration, without any file-specific flags')
    parser.add_argument('--single-run', '-s', help='Run only one file')
    parser.add_argument('--verbose', '-v', help='extra output', action='store_true')
    parser.add_argument('--board', '-b', type=str, help='specify board, if other than x86 (default)')
    parser.add_argument('--extra-clang-flags', '-c', type=str, help='Extra flags to pass to `clang`')
    parser.add_argument('--extra-opt-flags', '-o', type=str, help='Extra flags to pass to `opt`')
    parser.add_argument('--extra-link-flags', '-l', type=str, help='Extra flags to pass to linker')
    parser.add_argument('--extra-lc-flags', '-a', type=str, help='Extra flags to pass to `llc`, the assembler')
    args = parser.parse_args()
    returnVal = 0

    # process some args
    if args.verbose:
        verboseFlag = True
    if args.board:
        boardFlag = args.board
    else:
        boardFlag = None
    coast_config = args.config.lstrip()

    # test dir
    dir_path = os.path.dirname(os.path.realpath(__file__))

    # only run 1 test
    if args.single_run:
        # validate the test name
        single = [x for x in customConfigs if x.fname == args.single_run]
        returnVal = 0
        if len(single) > 0:
            singleFlag = True
            # add extra flags
            singleCfg = addMoreFlags(single[0], args)
            # run it!
            returnVal = run(singleCfg, coast_config, dir_path, board=boardFlag, no_clean=True)
        else:
            print("File name not found!")
        return returnVal

    # run all the tests
    else:
        for cfg in customConfigs:
            if cfg.hardKill:
                # don't test infinite loops automatically
                # TODO: make it so it kills it itself
                continue
            cfg = addMoreFlags(cfg, args)
            returnVal = run(cfg, coast_config, dir_path, board=boardFlag)
            if returnVal != 0:
                return returnVal
    # clean one more time if we did all of them
    clean = subprocess.Popen(['make', '-C', dir_path, 'clean'])
    clean.wait()

    if returnVal == 0:
        print("Success!")
    return returnVal


if __name__ == "__main__":
    returnVal = main()
    sys.exit(returnVal)
