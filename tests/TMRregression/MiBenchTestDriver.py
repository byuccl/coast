#!/usr/bin/python3

##############################################################################
# used to run the MiBench tests through TMR
#   found at: http://vhosts.eecs.umich.edu/mibench/index.html
# will run the tests unmitigated and with TMR
# then compare the resulting files with diff, output will be captured (WIP)
# will also capture any compilation errors
#
# changes made to source files:
#   /network/patricia - changed to exit(0) at end of program instead of exit(1)
#   /automotive/bitcount - added inline attribute of -replicateFnCalls
#       to the function ntbl_bitcnt
#   unlikely that /automotive/qsort will be supported because it constains
#       `scanf`. Same with /network/dijkstra
#   /automotive/susan - contains `fgets`, unlikely to get support
#       same with /network/patricia
##############################################################################

import os
import sys
import shlex
import datetime
import shutil
import glob
import argparse
import json     # for testing purposes
import subprocess as sp
from TMRfullTestSuite import centerText
from TMRregressionTest import printProgress

def runMakeClean():
    cmd = ['make', 'clean']
    p = sp.Popen(cmd, stdout=sp.PIPE)
    output = p.communicate()[0].decode()

# returns a string of the format
#   mon_day_hr_min
def getDateString():
    nowTime = datetime.datetime.now()
    return nowTime.strftime("%b-%d-%H-%M")

# still TODO:
#   compare the results of TMR and normal to see if they're the same
#       (use difflib)
#   create a nice display of which test is being run

# turns a list into a string with each element on a new line and indented
def listAsString(l):
    result = ""
    for line in l:
        if type(line) == type([]):      # maybe do "is" call instead?
            for sub_line in line:
                result += ("\t" + sub_line + "\n")
        else:
            result += ("\t" + line + "\n")
    return result

def checkOutputs(fldr):
    # get a list of all of the subfolders
    logName = 'checkOutputs.log'
    lf = open(os.path.join(fldr, logName), 'w')
    sub_dirs = sorted(os.listdir(fldr))
    for sd in sub_dirs:
        # only operate on the directories
        currFldr = os.path.join(fldr, sd)
        if os.path.isdir(currFldr):
            lf.write(centerText(" In folder: {} ".format(sd)) + "\n")
            # get a list of all of the files in there that have "output" in the name
            # in order so can compare two adjacent files
            files = sorted(glob.glob1(currFldr, 'output*'))
            for x in range(0, len(files),2):
                f1 = files[x]
                f2 = files[x+1]
                lf.write("Checking similarity of \n\t{} and\n\t{}\n".format(f1, f2))
                # run diff on these
                p0 = sp.Popen(['diff', os.path.join(currFldr, f1), os.path.join(currFldr, f2)], stdout=sp.PIPE)
                rslt = p0.communicate()[0]
                rc = p0.returncode
                if rc:
                    # print(rslt.decode())
                    lf.write("*** Files are not the same! ***\n")
        lf.write('\n')
    lf.close()
    return

# prints in the top level log file what is currently being tested
# arguments are: folder, targets, source files, commands to run, test #, and
#   log file to print to
def printTestHead(f, t, s, c, x, logFile):
    logFile.write(centerText(" Test # " + str(x)) + "\n")
    str1 = "Testing in\n\t{}\nMaking targets\n{}From source\n{}Running Commands\n{}".format(f, listAsString(t), listAsString(s), listAsString(c))
    logFile.write(str1)

# this function will print result details only if things did not pass
def printTestResult(results, logFile):
    logFile.write(centerText(" Test " + results["name"] + " ") + "\n")
    logFile.write("\n")
    for x in range(0,2):
        for k, log in results["test_log" + str(x)].items():
            # print(k + "  " + str(type(log)))
            comp_name = os.path.basename(log["compile_log"][0]).split(".")[0]
            if log["compile_result"] != 0:
                logFile.write("Error compiling " + comp_name)
                logFile.write(", see \n\t" + log["compile_log"][0] + "\n")
            else:
                logFile.write("Passed compilation " + comp_name + "\n")
                # for key, val in log.items():
                #     print("  " + key + "  " + str(type(val)))
                for i in range(len(log["exec_result"])):
                    cmd = results["commands"][x][i]
                    rc = log["exec_result"][i]
                    # print(cmd)
                    # print(rc)
                    if rc == 0:
                        logFile.write("Passed command " + cmd + "\n")
                    else:
                        logFile.write("Failed running command \n\t" + cmd + "\n")

    logFile.write("\n")
    # json.dump(results, logFile, indent='\t')

# moves files from the current directory to a different one
# arguments are: the destination folder, the string to use to find the file,
#   the string to append to the beginning of the file name
# returns a list of the new names of all of the files moved
def moveFiles(dstFolder, searchStr, prefixStr):
    srcFolder = os.getcwd()
    x = sp.Popen(['ls'],stdout=sp.PIPE)
    # grep for the thing
    y = sp.Popen(['grep', searchStr], stdin=x.stdout, stdout=sp.PIPE)
    files = y.communicate()[0].decode().split()
    # keep track of all new paths
    newPaths = []
    for f in files:
        srcPath = os.path.join(srcFolder, f)
        fname, ext = os.path.splitext(f)        # split the name by extension
        ii = 1
        cnt_str = ""
        while True:
            if ii > 1:
                cnt_str = "_" + str(ii)
                # print("Renaming log file with extenstion " + str(ii))
            if prefixStr:
                dstFile1 = "{}{}{}{}".format(fname, prefixStr, cnt_str, ext)
            else:
                dstFile1 = "{}{}{}".format(fname, cnt_str, ext)
            dstPath = os.path.join(dstFolder, dstFile1)
            # check if the file exists already
            if not os.path.exists(dstPath):
                break
            ii += 1
        # if prefixStr:
        #     dstFile1 = prefixStr + "_" + f
        # else:
        #     dstFile1 = f
        shutil.move(srcPath, dstPath)
        newPaths.append(dstPath)
    # print(str(newPaths))
    return newPaths

# creates a new folder underneath nowFolder to store subtest specific files
# returns path to new subfolder
def makeSubFolder(testFolder, progFolder, nowFolder):
    # runName = testFolder.split("/")
    # if not runName[len(runName)-1]:
    #     runName = runName[len(runName)-2]
    # else:
    #     runName = runName[len(runName)-1]
    runName = os.path.basename(os.path.dirname(testFolder))
    subFolder = os.path.join(progFolder, nowFolder)
    subFolder = os.path.join(subFolder, runName)
    os.makedirs(subFolder)
    return subFolder

# gets all dependencies from list (auxf) and moves them into progFolder
def importFileDependencies(auxf, fldr, progFolder):
    aux_list = []
    if auxf:
        for aux in auxf:
            aux_list += glob.glob(fldr + "*" + aux)
    for f in aux_list:
        shutil.copy(f, progFolder)
    return aux_list

# this function only does execution, all interpretation of results happens later
def run(fldr, trgts, srcs, auxf, cmds, logFile, nowFolder):
    progFolder = os.getcwd()
    # sometimes there is only one target that takes different parameters
    if len(trgts) == 1:
        trgts.append(trgts[0])
        srcs.append(srcs[0])

    # we are creating a dictionary to track everything
    run_log = {"name" : os.path.basename(os.path.dirname(fldr)),
                "folder" : fldr, "targets" : trgts, "sources" : srcs,
                "commands" : cmds, "dependencies" : auxf}
    # import any file dependencies
    aux_list = importFileDependencies(auxf, fldr, progFolder)
    # make a folder for this set of files
    subFolder = makeSubFolder(fldr, progFolder, nowFolder)
    # many of the tests need to be linked with the math library
    # this will probably change later to be more versatile
    xcflags = '""'
    xlflags = '"-lm"'
    # these are in quotes so it works for sure on the command line
    passes = ['""', '"-TMR"']

    for i in range(0,2):            # 0-2, exclusive of 2
        test_name = "test_log" + str(i)
        run_log[test_name] = {}       # need to compare across executions
        # going to have to do some tricky dictionary nesting
        for p in passes:
            # things specific to each run
            run_name = "run_log" + p.strip('"')
            run_log[test_name][run_name] = {}
            runMakeClean()
            command = ("make compile OPT_PASSES=" + p + " SRCFOLDER=\"" + fldr + "\" SRCFILES=\"" + srcs[i] + "\" XCFLAGS=" + xcflags + " XLFLAGS=" + xlflags + " TARGET=" + trgts[i])
            lname = trgts[i] + ".log"
            # check for file existing already; if so, rename
            # TODO: this check is seems to be always returning true
            # also doesn't catch when '-TMR' is appended to the front of the name
            # put the check when moving files
            # if os.path.exists(os.path.join(subFolder, lname)):
            #     lname = trgts[i] + '_2.log'
            with open(lname, 'w') as lf:
                lf.write(command + "\n")
                lf.flush()
                p1 = sp.Popen(shlex.split(command),stdout=lf, stderr=sp.STDOUT)
                p1.wait()
                rc = p1.returncode
                run_log[test_name][run_name]["compile_result"] = rc
                # the following list will be empty if compilation failed
                exec_results = []
                # if success, then rc = 0
                if not rc:
                    # now execute it
                    for cmd in cmds[i]:
                         exec_results.append(os.system(cmd))
                    run_log[test_name][run_name]["exec_result"] = exec_results
                    # move the resulting files
                    # note: these are not always .txt files
                    run_log[test_name][run_name]["files_created"] = moveFiles(subFolder, "output", trgts[i] + p.strip('"'))
                    # delete the executable
                    os.system("rm " + trgts[i])
                else:
                    # make note of error
                    lf.write("\n" + centerText(" compilation error, did not run "))
            # move the log file
            newLogFileName = moveFiles(subFolder, lname, p.strip('"'))
            run_log[test_name][run_name]["compile_log"] = newLogFileName
        # end inner for loop
    # end outer for loop
    # remove any dependencies copied in
    if auxf:
        for aux in aux_list:
            os.system("rm " + aux.split("/")[len(aux.split("/"))-1])
    return run_log

# similar to the one in TMRregressionTest
def printIntro(logFileName, numConfigurations, verboseFlag):
    os.system("reset")
    print("-----------------------------------------------------")
    print("TMR regression test suite - MiBench")
    print("-----------------------------------------------------")
    os.system("date")
    print("Testing in {} different folders".format(numConfigurations))
    print("Top level log file: " + logFileName)
    if verboseFlag:
        print("**Verbose mode enabled**")
    print("-----------------------------------------------------")
    print("Tests beginning...", end="\r")

def updateConsoleDisplay(srcFolder, totalRuns, testNum):
    print("Testing in: " + srcFolder)
    printProgress(testNum,totalRuns)
    print("\r", end="")

def main():
    parser = argparse.ArgumentParser(description="Run tests on the TMR pass using the MiBench test suite")
    parser.add_argument('--test', '-t', help='specify only one test to run (zero indexed)', type=int, nargs=1)
    parser.add_argument('--verbose', '-v', help='verbose mode, prints out results of trying to execute commands', action='store_true')
    parser.add_argument('--check', '-c', help='checks that outputs for normal and TMR are the same', nargs=1, metavar='folderName')
    args = parser.parse_args()
    verboseFlag = args.verbose

    progFolder = os.path.expanduser("~/llvm/tests/TMRregression/")
    # the output checker is a separate functionality
    # it will only run that, then exit
    if args.check:
        path = os.path.join(progFolder, args.check[0])
        print("Checking for valid outputs in:")
        print(" " + path + "...")
        checkOutputs(path)
        print("exiting...")
        return

    # it is a lot of work to figure out how to extract all of the following data
    #   automatically; it is actually less time consuming to just pull it all
    #   out by hand
    folder_list = ["~/llvm/tests/MiBench/automotive/basicmath/",
                    "~/llvm/tests/MiBench/automotive/bitcount/",
                    "~/llvm/tests/MiBench/automotive/qsort/",
                    "~/llvm/tests/MiBench/automotive/susan/",
                    "~/llvm/tests/MiBench/network/dijkstra/",
                    "~/llvm/tests/MiBench/network/patricia/"]
    target_list = [
            ['basicmath_small',
             'basicmath_large'],
            ['bitcnts'],
            ['qsort_small',
             'qsort_large'],
            ['susan'],
            ['dijkstra_small',
             'dijkstra_large'],
            ['patricia']
        ]
    source_list = [
            ['basicmath_small.c   rad2deg.c  cubic.c   isqrt.c',
             'basicmath_large.c   rad2deg.c  cubic.c   isqrt.c'],
            ['bitcnt_1.c bitcnt_2.c bitcnt_3.c bitcnt_4.c bitcnts.c bitfiles.c bitstrng.c bstr_i.c '],
            ['qsort_small.c',
             'qsort_large.c'],
            ['susan.c'],
            ['dijkstra_small.c',
             'dijkstra_large.c'],
            ['patricia.c patricia_test.c']
        ]
    aux_file_list = [
            [],
            [],
            ['.dat'],
            ['.pgm'],
            ['.dat'],
            ['.udp']
    ]
    # commands copied exactly from scripts in the test suite packages,
    #  except added the "./" at the beginning
    command_list = [
            [['./basicmath_small > output_small.txt'],
             ['./basicmath_large > output_large.txt']],
            [['./bitcnts 75000 > output_small.txt'],
             ['./bitcnts 1125000 > output_large.txt']],
            [['./qsort_small input_small.dat > output_small.txt'],
             ['./qsort_large input_large.dat > output_large.txt']],
            [['./susan input_small.pgm output_small.smoothing.pgm -s',
              './susan input_small.pgm output_small.edges.pgm -e',
              './susan input_small.pgm output_small.corners.pgm -c'],
             ['./susan input_large.pgm output_large.smoothing.pgm -s',
              './susan input_large.pgm output_large.edges.pgm -e',
              './susan input_large.pgm output_large.corners.pgm -c']],
            [['./dijkstra_small input.dat > output_small.dat'],
             ['./dijkstra_large input.dat > output_large.dat']],
            [['./patricia small.udp > output_small.txt'],
             ['./patricia large.udp > output_large.txt']]
        ]

    # create a folder for all of the auxiliary log files
    nowString = getDateString()
    nowFolder = os.path.join(progFolder, "MiBench_logs_" + nowString)
    os.makedirs(nowFolder)
    numConfigurations = len(folder_list)
    topLogFileName = "MiBench.log"

    with open(topLogFileName, 'w') as logFile:
        printIntro(logFile.name, numConfigurations, verboseFlag)
        for x in range(len(folder_list)):
            # sometimes only want to do one
            if args.test:
                x = args.test[0]
            fldr = os.path.expanduser(folder_list[x])
            trgts = target_list[x]
            srcs = source_list[x]
            auxf = aux_file_list[x]
            cmds = command_list[x]
            # printTestHead(fldr, trgts, srcs, cmds, x, logFile)
            updateConsoleDisplay(fldr, numConfigurations, x)
            success = run(fldr, trgts, srcs, auxf, cmds, logFile, nowFolder)
            printTestResult(success, logFile)
            # sometimes only want to do one
            if args.test:
                break
        runMakeClean()
        print("Complete! (" + str(numConfigurations) + "/" + str(numConfigurations) + ")                 " )
    # move top log file
    shutil.move(topLogFileName, nowFolder)

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\nInterrupted")
        try:
            sys.exit(0)
        except SystemExit:
            os._exit(0)
