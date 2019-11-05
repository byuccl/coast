#!/usr/bin/python3

#########################################################################
# This program is intended to run the generic Makefile for TMR
#   on any folder specified.
# It currently only works with C and C++ files
#
#########################################################################

import sys
import os
import subprocess
import shlex
import re
import time
import datetime
import argparse
from enum import Enum

# all possible error types to find in the log files
# add any additional error types here
class e_type(Enum):
    opt_error = 0
    llvm_error = 1
    clang_error = 2
    linker_error = 3
    exec_error = 4
    misc_error = 5

# this will pull out all important stuff from one run at a time
# modifies global dictionary of errors
# errors found here should be appended to the master dictionary
# there will then be a dictionary of
#       key: error type
#       value: list of tuple (configuration, error message)
# returns False if no error, True if there was one
def parseOneRun(currentRun,verboseFlag,run_dict):
    # first check if this run had an error
    make_error_string = "make: ***"
    if not any(make_error_string in s for s in currentRun):
        # if it didn't, then just return right away
        return False

    #keep track of the configuration for later
    configuration = ""
    hex_pattern = re.compile('0[xX][0-9a-fA-F]+')
    # don't want to double count opt errors
    assertionFail = False
    # this flag will be checked at the end to make sure we correctly categorized
    #   the error
    foundError = False

    # run by index instead of value to make it possible to look ahead
    for i in range(len(currentRun)):
        line = currentRun[i]
        if line.startswith("make run") or line.startswith("make test"):
            configuration = line
        # first thing we want to catch is clang errors, because that will invalidate
        #   the rest of the error messages for that configuration
        if (line.count("error:") > 0) and line.startswith("/"):
            # we also want the next line to see what line of the C file it failed on
            line += currentRun[i+1]
            run_dict[e_type.clang_error].append((configuration, line))
            foundError = True
            # any other errors will be a result of this first error, don't keep going
            break
        if (line.startswith("opt:")):
            # opt error, probably an assertion fail
            run_dict[e_type.opt_error].append((configuration, line))
            assertionFail = True
            foundError = True
        elif (not assertionFail) and (line.startswith("#0") and re.search(hex_pattern, line)):
            # opt error of some unknown character
            run_dict[e_type.opt_error].append((configuration, "see log file for details\n"))
            foundError = True
        if line.startswith("LLVM ERROR:"):
            # LLVM error
            run_dict[e_type.llvm_error].append((configuration, line))
            foundError = True
        if line.startswith("clang-3.9: error:"):
            # this is probably a linking error
            run_dict[e_type.linker_error].append((configuration, line))
            foundError = True
        if line.startswith("./"):
            # most likely an execution error
            run_dict[e_type.exec_error].append((configuration, "see log file for details"))
            foundError = True

    # catches all errors missed by the above classification
    if not foundError:
        run_dict[e_type.misc_error].append((configuration, "see log file for details\n"))
    return True

# prints out the contents of the dictionary obtained from parsing the log file
def printSummary(run_dict,summaryFile,badRunCount,verboseFlag):
    # header with all of the errors and how many
    summaryFile.write("{} bad configurations\n".format(badRunCount))
    for error in range(len(e_type)):
        summaryFile.write("\t{} error(s) of type ".format(len(run_dict[e_type(error)])) + str(e_type(error)).split(".")[1] + "\n")
    summaryFile.write("\n")
    # details of each error, sorted by type
    # iterate by index instead of value to preserve order of types
    for error in range(len(e_type)):
        e_list = run_dict[e_type(error)]
        if e_list:
            str1 = "{0:{c}^{n}}".format((" {} {} error(s) ".format(len(e_list), str(e_type(error)).split(".")[1])), c="=", n=80)
            summaryFile.write(str1 + "\n")
            for run in e_list:
                summaryFile.write("Error with configuration:\n" + run[0])
                # verbosity: besides configuration, also print line to blame
                if verboseFlag:
                    summaryFile.write(run[1] + "\n")
                else:
                    summaryFile.write("\n")

# this parsing function will, beginning at every ===== line,
#   copy all of the stuff until the next ===== line, then
#   pass that to a separate parse function to pull out the important stuff
def parseOutput(logFileName,verboseFlag):
    runCount = 0
    badRunCount = 0
    beginRun = "=================================="
    starting = True
    # initialize dictionary
    run_dict = {}
    for e in e_type:
        run_dict[e] = []

    currentRun = []
    with open(logFileName, 'r') as logFile:
        summaryFile = open(logFileName+".summary","w")
        for line in logFile:
            # only done at the beginning of the file
            if starting:
                if line.startswith(beginRun):
                    starting = False
                else:
                    # copy the timestamp
                    summaryFile.write(line)
                continue
            # copy all of the lines from one compile run to be parsed
            if line.startswith(beginRun):
                # escape case
                if parseOneRun(currentRun,verboseFlag,run_dict):
                    badRunCount += 1
                runCount += 1
                # reset the current run
                currentRun = []
            else:
                currentRun.append(line)
        printSummary(run_dict,summaryFile,badRunCount,verboseFlag)
        summaryFile.close()
    return runCount

def printIntro(logFileName,OPTS,OPT_LEVELS,srcFolder):
    os.system("reset")
    print("-----------------------------------------------------")
    print("TMR regression test suite")
    print("-----------------------------------------------------")
    os.system("date")
    print("Testing in: " + srcFolder)
    print("Writing to: " + logFileName)
    print("Passes: ",end="")
    print(*OPTS, sep=", ")
    print("Optimization levels: ",end="")
    print(*OPT_LEVELS,sep=", ")
    print("-----------------------------------------------------")
    print("Tests beginning...", end="\r")
    os.system("make clean")
    with open(logFileName, 'w') as logFile:
        now = subprocess.Popen("date", stdout=subprocess.PIPE)
        time = now.communicate()[0].decode()
        logFile.write("Test beginning at " + time)
        logFile.write("\n==================================\n\n")

def printProgress(runNumber,totalRuns):
    percentage = round(runNumber/totalRuns * 100,1)
    print("Progress: "+str(percentage)+"% (" + str(runNumber) + "/" + str(totalRuns) + ")",end="\r")

def run(opt,lvl,srcfolder,srcfiles,logFileName,logFile,compileOnly):
    if compileOnly:
        cmd = "test"
    else:
        cmd = "run"
    command = ("make " + cmd + " OPT_FLAGS=" + lvl + \
    " OPT_PASSES=" + opt + " SRCFOLDER=" + srcfolder + " SRCFILES=" + srcfiles)
    os.system("echo " + command + ">>" + logFileName)
    p = subprocess.Popen(shlex.split(command),stdout=logFile, stderr=subprocess.STDOUT)
    p.wait()
    os.system("echo '\n==================================\n' >> " + logFileName)
    os.system("make clean")

# looks at a specific file in a specific folder
# returns True if the file has "int main()" in it, False otherwise
def hasMainFunction(srcFileName,baseFolder):
    search_result = subprocess.Popen(['grep','-Hr','int main()',baseFolder + srcFileName], stdout=subprocess.PIPE)
    output, err_msg = search_result.communicate()
    if output:
        return True
    else:
        return False

def main():
    # parse command line arguments
    # run `python3 TMRregressionTest.py -h` to see options
    verboseFlag = False
    parseOnlyFlag = False
    parser = argparse.ArgumentParser(description="Process commands for TMR regression test")
    parser.add_argument('baseFolder', help='folder this program looks in to start running the TMR regression test')
    parser.add_argument('--verbose_mode', '-v', help='verbose mode', action="store_true")
    parser.add_argument('--parse_only', '-p', help='only parse a log file', metavar="logfile_name")
    args = parser.parse_args()
    if args.verbose_mode:
        verboseFlag = True
    if args.parse_only:
        parseOnlyFlag = True

    # make the log file have a timestamp in the name
    now = datetime.datetime.now()
    logFileName = "regResults-{}-{}-{}-{}.log".format(now.month, now.day, now.hour, now.minute)
    # which pass configurations to run
    # OPTS = ["", "\"-TMR -s\"", "\"-TMR -i\""]
    OPTS = ["", "\"-DWC\"", "\"-TMR\"", "\"-TMR -s -countErrors\"",
			"\"-DWC -noMemReplication\"", "\"-TMR -noMemReplication\"",
			"\"-DWC -noLoadSync\"", "\"-TMR -noLoadSync\"",
			"\"-DWC -noStoreDataSync\"", "\"-TMR -noStoreDataSync\"",
			"\"-DWC -noStoreAddrSync\"", "\"-TMR -noStoreAddrSync\"",
            "\"-DWC -noMemReplication -noLoadSync\"",
            "\"-TMR -noMemReplication -noLoadSync\"",
            "\"-DWC -noMemReplication -noStoreDataSync\"",
            "\"-TMR -noMemReplication -noStoreDataSync\"",
            "\"-DWC -noMemReplication -noStoreAddrSync\"",
            "\"-TMR -noMemReplication -noStoreAddrSync\""
            ]
    # optimization levels passed to clang
    OPT_LEVELS = [" ","-O2 "]
    # base folder for tests
    # baseFolder = os.path.expanduser("~/coast/llvm-project/clang/test/CodeGen/")
    baseFolder = os.path.expanduser(args.baseFolder)
    # get a list of all the targets to test
    # execute the ones that have "int main()" in them; the rest will be compiled
    all_file_list = os.listdir(baseFolder)
    file_list = []
    # for now, remove all of the things that aren't .c or .cpp files from the list
    for f in all_file_list:
        if os.path.isdir(f):
            print(f)
            continue
        f_split = f.split(".")
        ext = f_split[len(f_split)-1]
        if ext == "c" or ext == "cpp":
            file_list.append(f)

    hasMain_list = []
    compileOnly_list = []
    for f in file_list:
        if hasMainFunction(f,baseFolder):
            hasMain_list.append(f)
        else:
            compileOnly_list.append(f)

    start = time.time()
    if not parseOnlyFlag:
        # how many different configurations are there?
        numConfigurations = len(OPTS)*len(OPT_LEVELS)*len(file_list)
        runNumber = 0
        # this introduction shows up at the head of the terminal
        printIntro(logFileName,OPTS,OPT_LEVELS,baseFolder)
        logFile=open(logFileName,"a")
        for mFile in hasMain_list:
            for opt in OPTS:
                for lvl in OPT_LEVELS:
                    run(opt,lvl,baseFolder,mFile,logFileName,logFile,False)
                    runNumber += 1
                    printProgress(runNumber,numConfigurations)
        for cFile in compileOnly_list:
            for opt in OPTS:
                for lvl in OPT_LEVELS:
                    run(opt,lvl,baseFolder,cFile,logFileName,logFile,True)
                    runNumber += 1
                    printProgress(runNumber,numConfigurations)

        print("Complete! (" + str(numConfigurations) + "/" + str(numConfigurations) + ")            ")
        os.system("make clean")
        logFile.close()
    else:
        print("processing output only\n\tlog file: " + args.parse_only)
        logFileName = args.parse_only

    runCount = parseOutput(logFileName,verboseFlag)

    done = time.time()
    os.system("date")
    print("Time elapsed: " + str(int((done - start)/60)) + " minutes")


if __name__ == "__main__":
    main()
