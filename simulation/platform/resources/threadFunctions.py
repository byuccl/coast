"""Contains functions that are targets for supervisor threads."""

# library modules
import sys
import json
import time
import signal
import socket
import threading
import faulthandler
from queue import Queue, Empty
from random import uniform, randint
from numpy import geomspace

# project modules
import resources.decoder as dec
import resources.utils as utils
import resources.colors as colors
import resources.timing as timing
import resources.network as network
import resources.strings as strings
from resources.benchmarks import getUpperTimeBound, getBpLocation
from resources.supportClasses import (  LogQueueMessage,
                                        UartQueueMsg,
                                        InjectionLog,
                                        RunResult,
                                        InvalidResult,
                                        TimeoutResult,
                                        AssertionFailResult,
                                        AbortResult,
                                        StackOverflowResult,
                                        CommonResult,
                                        ErrorMessage,
                                        InfoMessage,
                                        CacheInfo,
                                        gdbState)

# authorship information
__author__ = "Benjamin James"
__copyright__ = "Copyright 2019, Brigham Young University"


# global event flags for thread communication
resetSockFlag = threading.Event()

# global error count for stopping purposes
errorCount = 0


def emuListener(supervisor, logQ, uartQ, flag):
    """Decodes the output of emulator stdout."""
    decoder = dec.EmulatorDecoder()
    firstRunsCount = 2
    targetTime = None

    while not flag():
        server = supervisor.emulatorServer
        for line in iter(server.handle.stdout.readline, b''):
            if flag():
                break
            if server.handle.poll() is not None:
                time.sleep(0.1)
            underTimeFlag = False

            try:
                dline = line.decode('utf-8')
            except UnicodeDecodeError:
                # strange invalid results could give decode errors
                inv = InvalidResult(repr(dline))
                logQ.put(inv)
                continue

            nextData = decoder.parseline(dline)

            if firstRunsCount and isinstance(nextData, RunResult):
                if firstRunsCount == 1:
                    # has to be at least 10% of this time
                    targetTime = float(nextData.runTime) * 0.1
                firstRunsCount -= 1

            elif isinstance(nextData, RunResult):
                # check for running out of range
                # if not utils.withinFloatRange(nextData.runTime, targetTime, perc=0.5):
                if nextData.runTime < targetTime:
                    if nextData.isSuccess():
                        nextData.errors = 1
                        logQ.put(LogQueueMessage(strings.qSrcThreads,
                                "{} Setting error flag because of wrong runtime".format(nextData.ftime)))
                    # uartQ.put(UartQueueMsg(strings.underTimeMsg, None))
                    logQ.put(strings.underTimeMsg)
                    underTimeFlag = True

            if not underTimeFlag:
                logQ.put(strings.normalTimeMsg)

            if nextData is None:
                continue
            else:
                # send to the logging queue
                logQ.put(nextData)
                # tell the communicator thread about it
                if not (isinstance(nextData, InfoMessage) or isinstance(nextData, ErrorMessage)):
                    uartQ.put(nextData)


def gdbListener(supervisor, logQ, flag):
    """Target function for the GDB logfile decoder."""
    decoder = dec.GDBDecoder()

    while not flag():
        # this is to keep it running even if the gdb process is restarted
        gdb = supervisor.gdb
        for line in iter(gdb.stdout.readline, b''):
            # finish condition
            if flag():
                break
            if gdb.poll() is not None:
                time.sleep(0.1)

            dline = line.decode('utf-8').rstrip()
            nextData = decoder.parseline(dline)
            if nextData is not None:
                logQ.put(LogQueueMessage(strings.qSrcGDB, dline))

        # just in case, to avoid race conditions
        time.sleep(0.01)
    logQ.put(LogQueueMessage(strings.qSrcGDB, "GDB listener closed", False))


def queueListener(q, lf, jf, rrf, dbgInfo):
    nextResult = None
    resultRecordedFlag = rrf
    firstTimeWrite = True
    firstTimeFlag = False
    underTimeCount = 0

    while True:
        data = q.get()
        # end condition
        if data == strings.queueStopMsg:
            q.task_done()
            break

        elif isinstance(data, CommonResult):
            # print("Got data of type " + type(data).__name__)

            # if incoming is timeout and we already have abort, don't overwrite
            if isinstance(data, TimeoutResult) and \
                ( isinstance(nextResult, AbortResult) or \
                  isinstance(nextResult, StackOverflowResult) \
                ):
                # same with StackOverflowResult (and AssertionFail?)
                pass
            else:
                # we've got the UART output from a run
                nextResult = data

            if not firstTimeFlag:
                resultRecordedFlag.set()
            firstTimeFlag = False

            # rate-limit when it's broken
            if underTimeCount == 5:
                print(colors.colorMsg(" --- Truncating output ---", colors.tcolors.YELLOW))
                lf.write(" --- Truncating output ---\n")
            elif underTimeCount > 5:
                pass
            else:
                print(colors.colorMsg(nextResult, colors.tcolors.YELLOW))
                lf.write(str(nextResult) + '\n')

        # do we need to discard the above data?
        elif isinstance(data, str):
            if data == strings.discardUartResultCmd:
                resultRecordedFlag.clear()
                nextResult = None
                lf.write(' ' + data + '\n')
                lf.flush()
            elif data == strings.underTimeMsg:
                underTimeCount += 1
            elif data == strings.normalTimeMsg:
                underTimeCount = 0

        elif isinstance(data, InjectionLog):
            # print("Got data of type InjectionLog")
            resultRecordedFlag.clear()
            data.addRunLog(nextResult)
            print(colors.colorMsg(data, colors.tcolors.CYAN))
            lf.write(str(data) + '\n')
            # log the data to json
            if not firstTimeWrite:
                jf.write(",\n")
            firstTimeWrite = False
            json.dump(data.getDict(), jf, indent=4)
            # flush output buffers so we get the most updated data in the log files
            lf.flush()
            jf.flush()
            # this is needed for compatibility with tkill script
            sys.stdout.flush()
            # print(" -> Saving to JSON")
            # invalidate RunResult, to throw error if they get unaligned
            nextResult = None

        elif isinstance(data, ErrorMessage):
            print(colors.colorMsg(data.msg.strip(), colors.tcolors.YELLOW))

        elif isinstance(data, InfoMessage):
            if dbgInfo:
                print(colors.colorMsg(data.msg.strip(), colors.tcolors.YELLOW))

        elif isinstance(data, LogQueueMessage):
            if data.src == strings.qSrcGDB:
                print(colors.colorMsg("> " + data.msg, colors.tcolors.GREEN))
            elif data.src == strings.qSrcEmulator:
                print(colors.colorMsg(data.msg, colors.tcolors.YELLOW))
            elif data.src == strings.qSrcSocket:
                print(colors.colorMsg(data.msg, colors.tcolors.CYAN))
            elif data.src == strings.qSrcSupervisor:
                print(colors.colorMsg(data.msg, colors.tcolors.MAGENTA))
            elif data.src == strings.qSrcThreads:
                print(colors.colorMsg(data.msg, colors.tcolors.BLUE))
            elif data.src == strings.qSrcQueues:
                print(colors.colorMsg(data.msg, colors.tcolors.WHITE))
            else:
                # unidentified message
                print(colors.colorMsg(data.msg, colors.tcolors.BLACK))
            if data.log:
                lf.write(str(data.msg) + "\n")
        else:
            print(data)
        q.task_done()
    return



# have a single queue for sending commands
def gdbSendCommands(cmdQ, logQ, supervisor):
    """Reads each thing put in the queue and sends it to the GDB instance."""
    sock = supervisor.gdbSocket
    
    while True:
        cmd = cmdQ.get()
        # debug
        if supervisor.dbgCom:
            logQ.put(LogQueueMessage(strings.qSrcQueues, "- " + str(cmd), l=False))

        # when to be done
        if cmd == strings.finishCmd:
            print(colors.colorMsg("- GDB command queue finished", colors.tcolors.MAGENTA))
            break

        # need to reset the socket handle
        if cmd == strings.resetSocketCmd:
            # print("Reset gdb send socket handle")
            sock = supervisor.gdbSocket
            continue
    
        try:
            # send the message
            if isinstance(cmd, list):
                for c in cmd:
                    network.send_msg(sock, str(c))
            else:
                network.send_msg(sock, str(cmd))
        except OSError as e:
            print(e)
            print(cmd)
            raise e


def gdbReceiveResponses(msgQ, logQ, supervisor, quitListenFlag):
    """Listen for all responses from the GDB instance."""
    sock = supervisor.gdbSocket
    global resetSockFlag

    while True:
        if resetSockFlag.is_set():
            # then also check if we're done
            if quitListenFlag():
                print(colors.colorMsg("$ GDB message queue finished", colors.tcolors.MAGENTA))
                break

            # otherwise, reset the socket handle
            sock = supervisor.gdbSocket
            resetSockFlag.clear()

        # get from socket
        try:
            resp = network.recv_msg(sock, silent=True)
        except ConnectionError:
            # [Errno 9] Bad file descriptor
            # we now wait for the reset command
            if resetSockFlag.wait(timeout=30):
                continue
            else:
                print(colors.colorMsg("Error, socket was not reset within 30 seconds", 
                        colors.tcolors.RED), file=sys.stderr)
                # sys.exit(1)

        # decode if necessary
        if isinstance(resp, bytes):
            resp = resp.decode('utf-8')
        # stick it on the queue
        msgQ.put(resp)

        if supervisor.dbgCom:
            logQ.put(LogQueueMessage(strings.qSrcQueues, "$ " + str(resp), l=False))

        # finish conditions
        if quitListenFlag():
            print(colors.colorMsg("$ GDB message queue finished", colors.tcolors.MAGENTA))
            break


def gdbCommunicator(supervisor, logQ, uartQ, rrf):
    """Communicates with the GDB client over a socket."""

    def clientPrint(msg, log=True):
        """Inner function to print to logging queue"""
        nonlocal logQ
        logQ.put(LogQueueMessage(strings.qSrcSocket, msg, log))

    ######################### debug signal handler #########################
    faulthandler.enable(all_threads=True)
    faulthandler.register(signal.SIGUSR1, all_threads=True)

    ########################### set up variables ###########################
    emulator = supervisor.emulator
    maxInjections = supervisor.maxInjections
    maxErrors = supervisor.errorCountMatch
    injector = supervisor.injector
    injector.emulatorServer = supervisor.emulatorServer
    cacheInjectFlag = ("cache" in supervisor.sectionToInject)
    usePluginFlag = ( (supervisor.board == "pynq") or cacheInjectFlag)
    emulatorServer = supervisor.emulatorServer if usePluginFlag else None
    global errorCount
    
    resultRecordedFlag = rrf
    breakIteration = supervisor.injector.breaking.iteration
    if breakIteration != -1:
        breakSleep = supervisor.injector.breaking.sleepTime
    PCreg = supervisor.regCls.pc
    # for later
    maxUartWait = 5

    # more precise timing
    sleeper = timing.Sleeper()

    def resetHandles(normalFlag):
        nonlocal supervisor, emulator, injector, emulatorServer
        nonlocal usePluginFlag, msgQ, cmdQ, maxUartWait
        global resetSockFlag
        # empty the msg queue
        utils.emptyQueue(msgQ)
        # reset handles
        emulator = supervisor.emulator
        supervisor.emulatorServer.setSockTimeout(maxUartWait)
        injector.emulatorServer = supervisor.emulatorServer
        if usePluginFlag:
            emulatorServer = supervisor.emulatorServer
        # tell the queue listeners to reset the socket handles
        resetSockFlag.set()
        cmdQ.put(strings.resetSocketCmd)
        while resetSockFlag.is_set():
            pass


    ######################## GDB command interface #########################
    # cmdQ is for messages to send to the GDB instance
    # msgQ is for commands for this thread to act on
    msgQ = Queue()
    cmdQ = Queue()
    # flag to reset the socket handles
    global resetSockFlag
    # flag for finished listening
    quitListenFlag = False
    # thread setup
    gdbSendThread = threading.Thread(target=gdbSendCommands,
            args=(cmdQ, logQ, supervisor), name="gdbSendCommands")
    gdbReceiveThread = threading.Thread(target=gdbReceiveResponses,
            args=(msgQ, logQ, supervisor, lambda: quitListenFlag), name="gdbReceiveResponses")
    gdbSendThread.start()
    gdbReceiveThread.start()
    # set some handles
    injector.setQueues(msgQ, cmdQ)

    ########################### get timing data ############################
    # the first thing we do is measure how long we have
    #  in which to inject faults
    # we assume that all the breakpoints are set up by now 
    # it's paused at entry, so just continue
    if usePluginFlag:
        # where does main() start?
        cmdQ.put([strings.symbolAddrCmd, "main"])
        mainStart = msgQ.get()  # base 10 number, as string
        if supervisor.dbgCom:
            clientPrint("main() starts at {}".format(mainStart))
        # send the address
        emulatorServer.sendMsg(mainStart)
        # continue
        cmdQ.put(strings.contCmd)
        # get the cycle count
        startTimeStamp = int(str(emulatorServer.recvMsg(), encoding='utf-8'), 16)
        if supervisor.dbgCom:
            clientPrint("startTimeStamp = {}".format(startTimeStamp))
        msgQ.get()  # stop message - since breakpoint is after main()
    else:
        cmdQ.put(strings.contCmd)
        msgQ.get()  # stop message
        # let it go one more time to account for warm caches
        cmdQ.put(strings.contCmd)
        msgQ.get()  # stop message
        # now it's stopped, get the global timer value
        cmdQ.put(strings.readGlblTimerCmd)
        startTimeStamp = int(msgQ.get())

    # start local timer also
    t0 = time.perf_counter()
    # keep going until next breakpoint
    cmdQ.put(strings.contCmd)
    msgQ.get()  # stop message
    # next time stamp
    t1 = time.perf_counter()
    if usePluginFlag:
        # stop the emulator so the plugin exits
        supervisor.stopEmulatorOnly()
        # it will send the counted instructions over the socket
        endTimeStamp = int(supervisor.emulatorServer.recvMsg(), 16)
        # restart the emulator and reset handles
        supervisor.startEmulatorOnly(doInject=True)
        resetHandles(normalFlag=True)
    else:
        cmdQ.put(strings.readGlblTimerCmd)
        endTimeStamp = int(msgQ.get())

    # compute
    # clientPrint("start: {}, end: {}".format(startTimeStamp, endTimeStamp))
    timerPeriod = endTimeStamp - startTimeStamp
    clientPrint("Max cycles = {}".format(timerPeriod))
    coarseRunTime = t1 - t0
    clientPrint("Client measured: {:.6f}\n".format(coarseRunTime))
    maxUartWait = coarseRunTime * 3
    # make sure it's not miniscule and missing because of system timings
    if maxUartWait < 1.0:
        maxUartWait = 1.0
    cyclePeriod = int(endTimeStamp * 0.95)
    maxOutputTime = coarseRunTime * 20
    # set for the first time
    supervisor.emulatorServer.setSockTimeout(maxUartWait)

    ############################ set up bounds #############################
    supervisor.runtime = coarseRunTime
    cmdQ.put([strings.setTimeoutCmd, str(coarseRunTime)])
    msgQ.get()      # correctly set timeout
    # run for (max) twice what we measured
    upperBound = coarseRunTime * 2.0
    # python timing sleep can't go much smaller than this accurately,
    #  so we use a wrapper around the clib usleep function (timing.py)
    # anything below 0.001 has too much variance
    lowerBound = 0.001
    numSteps = 30
    # TODO: concerns that sleeping will always interrupt in
    #  roughly the same area of code execution

    # how to decrement the bound in case we sleep too long
    stepSpace = geomspace(upperBound, lowerBound, 
                        num=numSteps, endpoint=False)
    stepIdxTop = 0
    stepIdxBottom = len(stepSpace) - 1

    ########################### helper functions ###########################
    def changeBounds(current, down=True):
        """Helper function to adjust bounds as needed."""
        nonlocal stepSpace, stepIdxTop, stepIdxBottom
        nonlocal clientPrint
        returnVal = current
        stepDirection = None

        if stepIdxTop == stepIdxBottom:
            # we can't adjust any more
            returnVal = current
        elif down:
            nextDownIdx = stepIdxTop + 1
            if nextDownIdx == stepIdxBottom:
                returnVal = current
            else:
                # take a step downward
                stepIdxTop = nextDownIdx
                returnVal = stepSpace[stepIdxTop]
                stepDirection = "upper"
        else:
            nextUpIdx = stepIdxBottom - 1
            if nextUpIdx == stepIdxTop:
                returnVal = current
            else:
                # take a step upward
                stepIdxBottom = nextUpIdx
                returnVal = stepSpace[stepIdxBottom]
                stepDirection = "lower"

        if returnVal != current:
            # print if changing
            clientPrint("Adjusting {} bound to {:.6f}".format(
                stepDirection, returnVal
            ))
        return returnVal

    def getSleepTime():
        """Handles getting the number for sleeping."""
        nonlocal lowerBound, upperBound, breakIteration
        nonlocal usePluginFlag, supervisor, cyclePeriod
        # debug conditions
        if (supervisor.numInjections == breakIteration) or \
                (breakIteration == -2):
            sleepTime = breakSleep
        elif usePluginFlag:
            sleepTime = randint(startTimeStamp, cyclePeriod)
        else:
            sleepTime = uniform(lowerBound, upperBound)
        return sleepTime

    def checkIfHitBp(timeout=1.0):
        """This is a method of"""
        nonlocal msgQ, cmdQ, curState, clientPrint
        try:
            bpMsg = msgQ.get(timeout=timeout)
            if bpMsg == strings.bpMsg:
                # these come very close to each other, so small delay
                placeMsg = msgQ.get(timeout=0.1)
        except Empty:
            bpMsg = None
        return bpMsg

    def isInjectionFinished():
        """Returns true if we don't need to keep injecting faults.

        If maxErrorCount is defined, hit the next 1000 mark after that number
        is reached.
        Else if maxInjections is defined, follow that.
        """

        nonlocal supervisor, maxErrors, maxInjections
        nonlocal clientPrint
        global errorCount
        if maxErrors is not None:
            if errorCount >= maxErrors:
                # just do the rounding up once, then disable the maxErrorCount
                nearestThousand = round(supervisor.numInjections, -3)
                # https://stackoverflow.com/a/46481370/12940429
                if nearestThousand < supervisor.numInjections:
                    nearestThousand += 1000
                maxInjections = nearestThousand
                maxErrors = None
                clientPrint("Ending count set to {}".format(maxInjections))
                # could still have ended perfectly, so check it here as well
                return (supervisor.numInjections >= maxInjections)
            return False
        return (supervisor.numInjections >= maxInjections)

    ############################ finalize init #############################
    # finish setting up GDB
    cmdQ.put(strings.handlerSetupCmd)
    msgQ.get()      # set up GDB event handlers
    
    bpLocs = getBpLocation()
    usDiv = 1000000     # microsecond divider
    normalResetFlag = False

    # send a message to discard the UART results
    logQ.put(strings.discardUartResultCmd)
    # discard unused results
    utils.emptyQueue(uartQ)

    if usePluginFlag:
        curState = gdbState.reset
    else:
        curState = gdbState.injectFault

    ####################### monolithic state machine #######################
    while True:
        ############################ debug #############################
        if supervisor.dbgCom:
            clientPrint("# " + str(curState), log=False)

        ########################### finished ###########################
        if curState == gdbState.finished:
            break

        ######################### inject fault #########################
        elif curState == gdbState.injectFault:

            #################### common things #####################
            # make sure there are no pending uart messages
            utils.emptyQueue(uartQ)

            # how long to sleep
            sleepTime = getSleepTime()

            if usePluginFlag:
                # tell the sleep cycles now
                emulatorServer.sendMsg(str(sleepTime))
                # clientPrint("Sleeping for {} cycles".format(sleepTime))
            else:
                # pause the execution
                emulator.pause()
                cmdQ.put(strings.reloadCmd)


            ################### cache injection ####################
            if usePluginFlag:
                # continue until we hit the sleep condition
                # but we actually want it to skip the first time hitting the breakpoint,
                #  because this starts all the way back at cycle 0
                cmdQ.put(strings.contCmd)

                # see if it hit the breakpoint
                tmpMsg = checkIfHitBp(timeout=0.8)
                if tmpMsg == strings.bpMsg:
                    cmdQ.put(strings.contCmd)
                    pastFirstFlag = True        # indicates we've passed the first breakpoint
                elif tmpMsg is None:
                    pastFirstFlag = False
                else:
                    clientPrint("Some error! {}".format(str(tmpMsg)))
                    curState = gdbState.reset
                    continue

                try:
                    # the plugin will stop at the right spot, so no need to interrupt
                    faultResp, cycleCount = injector.injectFault(supervisor.numInjections,
                                                                disp=supervisor.dbgCom,
                                                                pluginFlag=usePluginFlag)
                    # get the cycle count at the end
                    diffStamp = cycleCount
                    # read the PC
                    cmdQ.put([strings.readRegCmd, PCreg.name])
                    PCval = msgQ.get(timeout=0.8)
                    discardResults = False
                except ValueError as ve:
                    # comes from problem converting message to int(), which means GDB died
                    clientPrint("Error, received message {} while injecting fault".format(str(ve)))
                    curState = gdbState.reset
                    continue
                except Empty:
                    # waiting for the queue to respond took too long
                    #  probably means breakpoint happened at an unanticipated place
                    clientPrint("Error, took too long waiting for queue to respond")
                    curState = gdbState.reset
                    continue
                except socket.timeout:
                    # ran too long
                    discardResults = True
                    try:
                        bpMsg = msgQ.get(timeout=maxUartWait)
                        clientPrint("Waited too long ({:.6f} s), hit the next breakpoint".format(sleepTime))
                    except Empty:
                        curState = gdbState.reset
                        continue
                finally:
                    # for the output, we didn't actually sleep; this was a cycle count
                    sleepTime = None

            ################### other injection ####################
            else:
                # keep going until next breakpoint
                cmdQ.put(strings.contCmd)
                stopMsg = msgQ.get()  # stop message

                if stopMsg == strings.timeoutMsg:
                    curState = gdbState.reset
                    continue

                if stopMsg != strings.bpMsg:
                    clientPrint("Hit '{}', continuing".format(stopMsg))
                    # debugging problems
                    if stopMsg == strings.deadMsg and supervisor.numInjections > 0:
                        # TODO: reference 'faultResp' before assignment
                        print("sleep {}, set {} = {}".format(
                            sleepTime, faultResp.address, faultResp.newValue
                        ))
                        supervisor.dbgCom = True
                    # try again
                    curState = gdbState.reset
                    continue
                else:
                    # it returns the bp location next
                    msgQ.get()

                # get the cycle count
                cmdQ.put(strings.readGlblTimerCmd)
                beginStamp = int(msgQ.get())

                # continue execution until next break point
                cmdQ.put(strings.contCmd)

                # sleep, then pause
                sleeper.sleep(sleepTime)
                cmdQ.put(strings.interruptCmd)
                hitStopMsg = msgQ.get()

                # make sure we didn't just hit a breakpoint
                if hitStopMsg == strings.bpMsg:
                    clientPrint("Waited too long ({:.6f} s), hit the next breakpoint".format(sleepTime))
                    # empty the queue (bp location)
                    msgQ.get()
                    cmdQ.put([strings.getVarCmd, "nErrors"])
                    nErrors = msgQ.get()
                    if int(nErrors) == 0:
                        # adjust bounds
                        upperBound = changeBounds(upperBound, down=True)
                        curState = gdbState.reset
                        continue
                    else:
                        clientPrint("  nErrors = {}".format(nErrors))

                # get the cycle count at the end
                cmdQ.put(strings.readGlblTimerCmd)
                endStamp = msgQ.get()
                discardResults = False

                ##################### inject fault #####################
                cmdQ.put([strings.readRegCmd, PCreg.name])
                PCval = msgQ.get()
                faultResp = injector.injectFault(supervisor.numInjections)

                ########################################################

                # see if we waited too long
                diffStamp = int(endStamp) - beginStamp
                # The cycle count is not the same every iteration,
                #  so we can't make such a tight constraint
                # Remove this for now
                # if diffStamp >= timerPeriod:
                #     # adjust
                #     clientPrint("diffStamp = {}".format(diffStamp))
                #     upperBound = changeBounds(upperBound, down=True)
                #     discardResults = True

                if diffStamp == 0:
                    # adjust
                    lowerBound = changeBounds(lowerBound, down=False)
                    discardResults = True

            # if not successful, try again in fault injection
            if not discardResults:
                curState = gdbState.getOutput
                faultResp.addInjectionInfo(sleepTime, diffStamp, PCval)
                cmdQ.put(strings.contCmd)
            else:
                # send a message to discard the UART results
                logQ.put(strings.discardUartResultCmd)

        ######################### read output ##########################
        elif curState == gdbState.getOutput:
            # check for the second breakpoint to be hit
            if usePluginFlag and (not pastFirstFlag):
                tmpMsg = checkIfHitBp(timeout=coarseRunTime)
                if tmpMsg == strings.bpMsg:
                    cmdQ.put(strings.contCmd)
                elif tmpMsg is None:
                    pass
                else:
                    clientPrint("Some error! {}".format(str(tmpMsg)))
                    curState = gdbState.reset
                    continue
            # we expect to hit a breakpoint
            try:
                stopMsg = msgQ.get(timeout=maxUartWait)
            except Empty:
                if supervisor.dbgCom:
                    clientPrint("Stuck waiting for output")
                curState = gdbState.timeout
                continue
            # but it's possible to hit something else instead
            if stopMsg == strings.timeoutMsg:
                if supervisor.dbgCom:
                    clientPrint("Execution timed out")
                curState = gdbState.timeout
                continue
            elif stopMsg == strings.deadMsg:
                curState = gdbState.dead
                continue
            elif stopMsg != strings.bpMsg:
                print("Unexpected message {}".format(stopMsg))
                curState = gdbState.dead
                continue

            # get the UART output (if any)
            try:
                uartResult = uartQ.get(timeout=maxUartWait)
                if isinstance(uartResult, InvalidResult):
                    clientPrint("Invalid output")
                elif isinstance(uartResult, AssertionFailResult):
                    clientPrint("Assertion failed: {}".format(str(uartResult)))
                elif isinstance(uartResult, AbortResult):
                    clientPrint(str(uartResult))
                elif isinstance(uartResult, StackOverflowResult):
                    clientPrint(str(uartResult))
                elif uartResult.isSuccess():
                    # check for sneaky errors
                    if uartResult.runTime > maxOutputTime:
                        # ran way too long (or invalid output that looks valid)
                        uartResult.errors = 1
                        clientPrint("There were {} errors last run".format(uartResult.errors))
                        errorCount += 1
                elif uartResult.hasError():
                    clientPrint("There were {} errors last run".format(uartResult.errors))
                    errorCount += 1
                elif uartResult.hasFault():
                    clientPrint("There were {} faults last run".format(uartResult.faults))
                else:
                    clientPrint("Successful run")
            except Empty:
                clientPrint("There was no UART output, assuming timeout")
                curState = gdbState.timeout
                continue

            # make sure we're at the right bp
            try:
                stopLoc = msgQ.get(timeout=1.0)
            except Empty:
                if supervisor.dbgCom:
                    clientPrint("Execution timed out")
                curState = gdbState.timeout
                continue
            if stopLoc not in bpLocs:
                clientPrint("Error, stopped at wrong bp: {}".format(stopLoc))
                curState = gdbState.dead
                continue

            # wait for the UART results to be recorded
            if not resultRecordedFlag.wait(timeout=maxUartWait):
                clientPrint("Error, exiting!")
                break
            logQ.put(faultResp)
            supervisor.numInjections += 1

            # Decide when to stop
            if isInjectionFinished():
                break
            else:
                curState = gdbState.reset

        ########################### timeout ############################
        elif curState == gdbState.timeout:
            # read the current PC value
            cmdQ.put(strings.interruptCmd)
            time.sleep(0.1)    # to avoid trying to execute commands while still running
            # wrap this in try to catch queue timeout
            try:
                cmdQ.put([strings.readRegCmd, PCreg.name])
                readVal = msgQ.get(timeout=1.0)
                # there could be other messages here
                if readVal == strings.stopMsg:
                    # read again
                    readVal = msgQ.get(timeout=1.0)

                if readVal == strings.deadMsg:
                    clientPrint("GDB died, restarting")
                    curState = gdbState.dead
                    continue
                elif readVal in bpLocs:
                    readVal = msgQ.get(timeout=1.0)
                elif readVal == strings.timeoutMsg:
                    # this means we got to this state some other way than by the message,
                    #  but then the message still showed up anyway
                    readVal = msgQ.get(timeout=1.0)
            except Empty:
                clientPrint("Reading timeout data failed, resetting...")
                curState = gdbState.dead
                continue

            try:
                timeoutPC = int(readVal, base=16)
            except ValueError as ve:
                print(ve)
                curState = gdbState.dead
                continue

            # add a timeout message to the log queue
            timeoutResult = TimeoutResult(
                    "timed out, PC at {}".format(hex(timeoutPC)))
            logQ.put(timeoutResult)

            # make sure it arrived before sending the next message
            if not resultRecordedFlag.wait(timeout=maxUartWait):
                clientPrint("Error, exiting!")
                break
            logQ.put(faultResp)
            supervisor.numInjections += 1

            # go back to fault injection again
            if isInjectionFinished():
                break
            else:
                curState = gdbState.reset

        ######################## reset emulator ########################
        elif curState == gdbState.reset:
            # normal reset, don't print anything
            normalResetFlag = True
            curState = gdbState.dead

        ########################## reset gdb ###########################
        elif curState == gdbState.dead:
            if not normalResetFlag:
                clientPrint("dead: sleep {}, set {} = {}".format(
                        sleepTime, faultResp.address, faultResp.newValue
                ))
            # kill the GDB process
            if normalResetFlag:
                cmdQ.put(strings.silentKillCmd)
            else:
                cmdQ.put(strings.killGdbCmd)
            # these are here to make sure GDB finishes the commands before we kill the remote target
            try:
                deadResp = msgQ.get(timeout=1.0)
                finishedResp = msgQ.get(timeout=1.0)
            except Empty:
                # don't care, just wanted to make sure they weren't just sitting there
                pass
            # restart the supervisor subprocesses
            try:
                supervisor.restart(silent=normalResetFlag, hard=(not normalResetFlag), doInject=True)
            except ConnectionError:
                # this means that something else has started using the port - exit
                break
            resetHandles(normalResetFlag)
            # go back to injecting faults
            curState = gdbState.injectFault
            normalResetFlag = False

        else:
            clientPrint("Error, unexpected state!")
            curState = gdbState.reset

    ############################### cleanup ################################
    clientPrint("GDB, quit please...", log=False)
    try:
        cmdQ.put(strings.quitCmd)
        reply = msgQ.get(timeout=1.0)
        clientPrint(reply, log=False)
        msgQ.get(timeout=1.0)
    except Empty:
        clientPrint("GDB quit responding")
    supervisor.stop()

    # end all the threads
    cmdQ.put(strings.finishCmd)
    quitListenFlag = True
    resetSockFlag.set()
    gdbSendThread.join()
    gdbReceiveThread.join()
