#!/usr/bin/python3

##############################################################################
#
# Run tests generated using the llvm-stress executable
# example command:
#   ./llvm-stress -seed $RANDOM -o tmp.ll -size 1000
#   ./llc tmp.ll -mcpu=corei7-avx -mattr=+avx -o /dev/null
#
##############################################################################

import os
import sys
import shlex
import time
import datetime
import shutil
import argparse
import subprocess as sp
from random import sample
from TMRfullTestSuite import centerText
from TMRregressionTest import printProgress
from MiBenchTestDriver import getDateString
from MiBenchTestDriver import moveFiles
from MiBenchTestDriver import runMakeClean

def setUpArgs():
    parser = argparse.ArgumentParser(description="Run randomly generated .ll files (using llvm-stress) through DWC and TMR passes")
    parser.add_argument('--tests', '-n', help='how many times to run the stress test (default 10)', type=int, nargs=1, default=[10])
    parser.add_argument('--size', '-s', help='size will be passed to llvm-stress. indicates number of lines to generate', nargs=1, type=int, default=150)
    parser.add_argument('--type', '-t', help='runs tests on each different type of IR scalar type (longer execution time)', action='store_true')
    parser.add_argument('--clean', '-c', help='gets rid of all files in /stressTests/ with the .log extension', action='store_true')
    return parser

# similar to the one in TMRregressionTest
def printIntro(logFileName, numConfigurations):
    os.system("reset")
    print("-----------------------------------------------------")
    print("TMR regression testing - Stress Tester")
    print("-----------------------------------------------------")
    os.system("date")
    print("Testing {} different files".format(numConfigurations))
    print("Log file: " + logFileName)
    print("-----------------------------------------------------")
    print("Tests beginning...", end="\r")

def updateConsoleDisplay(srcFile, totalRuns, testNum):
    print("Testing: " + srcFile)
    printProgress(testNum,totalRuns)
    print("\r", end="")

def createRandomIRFile(fldr, rand_file):
    cmd0 = "llvm-stress -o " + os.path.join(fldr,rand_file)
    # print("running command: " + cmd0)
    p0 = sp.Popen(shlex.split(cmd0),stdout=sp.PIPE)
    output = p0.communicate()[0]
    # wait for file to be created
    while not os.path.exists(os.path.join(fldr,rand_file)):
        time.sleep(0.5)
    return p0.returncode

def llvmAsseble(progFolder, fldr, rand_file, output_file):
    # + fldr + "/"
    cmd1 = "llvm-as " + fldr + "/" + rand_file + " -o "  + output_file
    # print("running command: " + cmd1)
    p1 = sp.Popen(shlex.split(cmd1), stdout=sp.PIPE)
    output = p1.communicate()[0]
    # wait for file to be created
    while not os.path.exists(os.path.join(progFolder,output_file)):
        time.sleep(0.5)
    return p1.returncode

def llvmOptimizer(fldr, output_file, opt_file, target, p, lf):
    xcflags = ""
    xlflags = ""
    # run through opt  #"\" BCFILES=\"" + output_file +#
    cmd2 = ("make " + opt_file + " OPT_PASSES=" + p + " SRCFOLDER=\"" + fldr +  "\" XCFLAGS=" + xcflags + " XLFLAGS=" + xlflags + " TARGET=" + target)
    # print("running command: " + cmd2)
    p2 = sp.Popen(shlex.split(cmd2),stdout=lf, stderr=sp.STDOUT)
    p2.wait()
    return p2.returncode

def main():
    # command line processing
    parser = setUpArgs()
    args = parser.parse_args()

    # set up folder references
    progFolder = os.getcwd()
    fldr = os.path.join(progFolder,"stressTests")
    # print("tests go in " + fldr)
    if args.clean:
        print("Removing old log files...")
        os.system("rm " + fldr + "/*.log")
        print("Removing old .bc files...")
        os.system("rm " + fldr + "/*.bc")
        print("Removing old .ll files...")
        os.system("rm " + fldr + "/*.ll")
        sys.exit(0)

    passes = ['"-DWC"', '"-TMR"']
    numRuns = args.tests[0]

    # header
    logFileName = "rand_log_" + getDateString() + ".log"
    printIntro(logFileName, numRuns)

    # keep track of everything
    overall_summary = []
    with open(os.path.join(fldr,logFileName), 'w') as lf:
        # do this 'n' number of times
        for x in range(0,numRuns):
            title = ' Test #{} '.format(x)
            lf.write(centerText(title) + '\n')
            lf.flush()
            # file name definitions
            target = "stress_random_" + str(x)
            rand_file = target + ".clang.ll"
            output_file = rand_file.replace("ll", "bc")
            opt_file = output_file.replace("clang", "opt")
            opt_dis_file = opt_file.replace("bc", "ll")
            run_summary = {"header" : title, "target" : target}

            updateConsoleDisplay(target, args.tests[0], x)
            # create random .ll files
            rc0 = createRandomIRFile(fldr, rand_file)
            # stuff = input("enter anything to continue")
            # convert to .bc files
            rc1 = llvmAsseble(progFolder, fldr, rand_file, output_file)
            # stuff2 = input("enter anything to continue")
            # run through opt
            p = sample(passes, 1)[0]
            rc2 = llvmOptimizer(fldr, output_file, opt_file, target, p, lf)
            run_summary["passes"] = p
            run_summary["opt_rc"] = rc2

            # move .ll files to test folder
            newLLfiles = moveFiles(fldr, opt_dis_file, "")
            if rc2 == 0:
                # print("moved files: \n\t" + str(newLLfiles))
                # os.system("rm " + fldr + "/*.bc")
                if newLLfiles:
                    # remove .ll files because passed test
                    os.system("rm " + newLLfiles[0])
            else:
                lf.write(centerText(' failed compilation! ') + "\n")
                lf.write('\tsee file ' + rand_file + '\n')
                lf.flush()
            # get rid of .bc files in the top folder
            runMakeClean()
            lf.write("\n")
            overall_summary.append(run_summary)

        # print the summary at the end
        lf.flush()
        print("Summarizing data....                         ")
        print("Complete! (" + str(numRuns) + "/" + str(numRuns) + ")              " )
        sum_head = centerText('') + '\n' + centerText(' Summary: ') + '\n' + centerText('') + '\n\n'
        lf.write(sum_head)
        successes = 0
        for summary in overall_summary:
            lf.write('----' + summary["header"] + '----\n')
            lf.write('Target: \t\t' + summary["target"] + '\n')
            lf.write('Running pass: \t' + summary["passes"] + '\n')
            if summary["opt_rc"] == 0:
                lf.write('Result: \t\tSuccess\n\n')
                successes += 1
            else:
                lf.write('Result: \t\tFailure\n\n')
        lf.write("---- Overall ----\n")
        lf.write("Successes: {}/{}\n".format(successes, numRuns))

    return


if __name__ == '__main__':
    main()
