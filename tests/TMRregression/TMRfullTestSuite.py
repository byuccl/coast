#!/usr/bin/python3

#########################################################################
# This program is a driver for the TMRregressionTest.py program.
# Given a list of folders in which C and C++ files can be found,
#   this program will run the regression test in each of the folders.
#
# This will also run TMR on Coremark, the FFT benchmarks, and
#   anything that has a specialized Makefile
#
#########################################################################

import os
import subprocess as sp
import datetime
import shutil
import importlib
from TMRregressionTest import printProgress

# returns a string of the format
#   mon_day_hr_min
def getDateString():
    nowTime = datetime.datetime.now()
    return nowTime.strftime("%b-%d-%H-%M")

# puts text in the center of multiple "="
def centerText(text):
    return "{0:{c}^{n}}".format(text, c='=', n=80)

# nearly the same as the one in TMRregressionTest, but not quite
def printIntro(srcFolder,logFileName,OPTS):
    os.system("reset")
    print("-----------------------------------------------------")
    print("TMR regression test suite")
    print("-----------------------------------------------------")
    os.system("date")
    print("Testing in: " + srcFolder)
    print("Writing to: " + logFileName)
    print("Passes: ",end="")
    print(*OPTS, sep=", ")
    print("-----------------------------------------------------")
    print("Tests beginning...", end="\r")

# this runs TMRregressionTest.py on each specified base folder
def runRegressionTest(d, cmd, testLog):
    cmd[2] = d
    p = sp.Popen(cmd)
    p.wait()
    # write to log file
    testLog.write("Finished testing in " + d + "at time ")
    testLog.write(str(datetime.datetime.now()) + "\n")

# move log files to time-stamped folder
def moveLogFiles(d, progFolder, nowFolder, testLog):
    x = sp.Popen(['ls'],stdout=sp.PIPE)
    y = sp.Popen(['grep','TMRregressionTestResults'], stdin=x.stdout, stdout=sp.PIPE)
    files = y.communicate()[0].decode().split()
    srcFolder1 = os.path.join(progFolder, files[0])
    srcFolder2 = os.path.join(progFolder, files[1])
    dstFolder1 = os.path.join(nowFolder, files[0])
    dstFolder2 = os.path.join(nowFolder, files[1])
    # if the test took less than a minute, the next one could possibly
    # overwrite the log file with the same name
    # we will append the name of the test folder to the log file to avoid this
    # and to increase readability
    d_sp = d.split("/")
    test_folder_name = d_sp[len(d_sp)-1]
    if not test_folder_name:
        test_folder_name = d_sp[len(d_sp)-2]
    dstFolder1 += ("." + test_folder_name)
    dstFolder2 += ("." + test_folder_name)
    testLog.write("See log files:\n\t" + dstFolder1 + "\nand\n\t" + dstFolder2 + "\n\n")
    shutil.move(srcFolder1, dstFolder1)
    shutil.move(srcFolder2, dstFolder2)

# returns the name of a log file to use by the custom benchmarks
def createNewLogfile(testName, nowFolder):
    nowTime = datetime.datetime.now()
    suffix = "log_{}_{}_{}_{}.log".format(testName, nowTime.day, nowTime.hour, nowTime.minute)
    return os.path.join(nowFolder, suffix)

def runMakeClean(path):
    cmd = ['make', '-C', path, 'clean']
    p = sp.Popen(cmd, stdout=sp.PIPE)
    output = p.communicate()[0].decode()
    # print(output)

#dst_file should already be open
def copyCoremarkLogfile(file_path, file_name, dst_file):
    clf = os.path.join(file_path, file_name)
    with open(clf, 'r') as lf:
        str1 = "{0:{c}^{n}}".format(("Coremark " + file_name), c='=', n=80)
        dst_file.write(str1 + "\n\n")
        for line in lf:
            dst_file.write(line)
        dst_file.write("\n")

# tests the Coremark benchmark, with and without TMR
# takes as input the top level log file,
#   and the folder where we put the log file from each run
def testCoremark(testLog, nowFolder):
    # Coremark:
    coremark_folder = os.path.expanduser("~/llvm/tests/coremark/")
    logFile = createNewLogfile("coremark", nowFolder)
    printIntro(coremark_folder,logFile,['', '-TMR'])
    with open(logFile, 'a') as lf:
        runMakeClean(coremark_folder)
        # run it without any TMR to get correct output values
        p = sp.Popen(['make', '-C', coremark_folder, 'run'], stdout=lf, stderr=lf, universal_newlines=True)
        p.wait()
        rc = p.returncode
        if not rc:
            # copy the coremark log files into the current log file
            copyCoremarkLogfile(coremark_folder, "run1.log", lf)
            copyCoremarkLogfile(coremark_folder, "run2.log", lf)
        else:
            lf.write("==== Error, did not execute! ====")
        printProgress(1,2)
        # flush the output buffer to make sure this shows up in the right place
        lf.flush()
        runMakeClean(coremark_folder)
        # run with TMR
        p = sp.Popen(['make', '-C', coremark_folder, 'run', 'OPT_PASSES="-TMR -replicateFnCalls=portable_malloc"'], universal_newlines=True, stdout=lf, stderr=lf)
        p.wait()
        rc = p.returncode
        if not rc:
            # copy the coremark log files into the current log file
            copyCoremarkLogfile(coremark_folder, "run1.log", lf)
            copyCoremarkLogfile(coremark_folder, "run2.log", lf)
        else:
            lf.write("==== Error, did not execute! ====")
        lf.flush()
        runMakeClean(coremark_folder)
    testLog.write("Finished testing in " + coremark_folder + "at time ")
    testLog.write(str(datetime.datetime.now()) + "\n")
    testLog.write("See log file\n\t" + logFile + "\n\n")
    print("Complete! (2/2)" )
    os.system("date")

def fft_subtests(fft_folder,lf):
    for x in range(1,5):
        lf.write("\n" + centerText(" Test " + str(x) + " ") + "\n")
        lf.flush
        p = sp.Popen(['make', '-C', fft_folder, 'test' + str(x), 'OPT_PASSES="-TMR -replicateFnCalls=KISS_FFT_MALLOC,kiss_fftr_alloc,kiss_fftnd_alloc,kiss_fftndr_alloc,kiss_fft_alloc"'], stdout=sp.PIPE, stderr=sp.PIPE, universal_newlines=True)
        output, errs = p.communicate()
        for line in output:
            lf.write(line)
        lf.flush()
        for line in errs:
            lf.write(line)
        lf.flush()
        printProgress(x+1,5)
        runMakeClean(fft_folder)
    return

def testKissFFT130(testLog, nowFolder):
    # kiss_fft130
    fft_folder = os.path.expanduser("~/llvm/tests/kiss_fft130/custom/")
    logFile = createNewLogfile("kiss_fft130", nowFolder)
    printIntro(fft_folder, logFile, ['', '-TMR'])
    with open(logFile, 'a') as lf:
        runMakeClean(fft_folder)
        # run it without any TMR to get correct output values
        lf.write("\n" + centerText(" Control test ") + "\n")
        lf.flush()
        p = sp.Popen(['make', '-C', fft_folder, 'test'], stdout=lf, stderr=lf, universal_newlines=True)
        p.wait()
        printProgress(1,5)
        runMakeClean(fft_folder)
        # now run it with TMR with each of the 4 tests
        fft_subtests(fft_folder,lf)
    testLog.write("Finished testing in " + fft_folder + "at time ")
    testLog.write(str(datetime.datetime.now()) + "\n")
    testLog.write("See log file\n\t" + logFile + "\n\n")
    print("Complete! (5/5)")
    os.system("date")

def main():
    progFolder = os.path.expanduser("~/llvm/tests/TMRregression/")
    # this command will be changed every run, and have added to it
    #   the name of the next folder to do the tests in
    cmd = ['python3', 'TMRregressionTest.py', '/dev/null']
    # all of the places to run the command
    test_dirs = ['~/llvm/llvm/tools/clang/test/ASTMerge/',
                 '~/llvm/tests/TMRregression/unitTests/',
                 '~/llvm/llvm/tools/clang/test/CodeGen/']
    now = datetime.datetime.now()
    # nowString = "{}-{}-{}".format(now.day, now.hour, now.minute)
    nowString = getDateString()
    testSuiteLogName = "testSuiteResults-" + nowString + ".log"
    nowFolder = os.path.join(progFolder, "logs_" + nowString)
    os.makedirs(nowFolder)
    with open(testSuiteLogName,'w') as testLog:
        # log file header
        now_time = sp.Popen("date", stdout=sp.PIPE).communicate()[0].decode()
        testLog.write("Beginning tests at " + now_time)
        testLog.write("Log files from each folder can be found in " + nowFolder)
        testLog.write("\n==================================\n\n")

        # for each test folder, run the command
        for d in test_dirs:
            runRegressionTest(d, cmd, testLog)
            moveLogFiles(d, progFolder, nowFolder, testLog)

        # now run the tests on the benchmarks with specialized makefiles
        testKissFFT130(testLog, nowFolder)
        testCoremark(testLog, nowFolder)
    # move the makefile
    newLogLocation = shutil.move(testSuiteLogName, nowFolder)
    print("Top level log file:\n\t" + newLogLocation)
    return

if __name__ == '__main__':
    main()
