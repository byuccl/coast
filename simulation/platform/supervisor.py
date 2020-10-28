#!/usr/bin/python3

"""Script to run & supervise a fault injection campaign on an Emulator."""

import os
import sys
import time
import shlex
import pickle
import socket
import argparse
import datetime
import threading
from queue import Queue
import subprocess as sp
import textwrap as _text_wrap

try:
    import resources.mem as mem
    import resources.utils as utils
    import resources.injector as inj
    import resources.network as network
    import resources.strings as strings
    import resources.interface as interface
    import resources.registers as registers
    import resources.benchmarks as benchmarks
    import resources.threadFunctions as threads
except ImportError as err:
    print("Error: ", err)
    sys.exit()


# https://stackoverflow.com/a/32974697
class MultilineFormatter(argparse.HelpFormatter):
    """Print argparse help text across multiple lines."""
    def _fill_text(self, text, width, indent):
        text = self._whitespace_matcher.sub(' ', text).strip()
        paragraphs = text.split('|n')
        multiline_text = ''
        for paragraph in paragraphs:
            formatted_paragraph = _text_wrap.fill(paragraph, width, initial_indent=indent, subsequent_indent=indent) + '\n'
            multiline_text = multiline_text + formatted_paragraph
        return multiline_text

    # https://stackoverflow.com/a/22157136
    def _split_lines(self, text, width):
        if text.startswith('N|'):
            text_new = text[2:].splitlines()
            retVal = []
            for l in text_new:
                retVal.extend(argparse.HelpFormatter._split_lines(self, l, width))
            return retVal
        elif text.startswith('R|'):
            return text[2:].splitlines()
        return argparse.HelpFormatter._split_lines(self, text, width)


class Supervisor(object):
    """Supervisor runs all of the subprocesses for a fault injection campaign."""
    def __init__(self, m, s, fn, b, pr, e=None):
        # ports - validated in arg parser
        portRange = range(pr, pr+5)
        self.gdbServerPortNum = portRange[0]
        self.gdbPythonPortNum = portRange[1]
        self.emulatorPortNum = portRange[2]
        usePlugin = ( ("cache" in s) or (b == "pynq") )
        self.cachePluginPortNum = portRange[3] if usePlugin else None
        # set up the board and benchmarks
        self.board = b
        self.filename = fn
        self.dirname = os.path.split(os.path.dirname(fn))[1]
        if not benchmarks.setBoardBenchmark(self.board, self.dirname):
            print("Invalid board or benchmark!", file=sys.stderr)
            sys.exit(-1)
        # memory map and output queue
        self.getMMap()
        self.outQ = None
        # gdb, emulator, and injector handles
        self.gdb = None
        self.gdbSocket = None
        self.emulatorServer = interface.EmulatorServer(
                fn,
                gdbPort=self.gdbServerPortNum,
                telnetPort=self.emulatorPortNum,
                pluginPort=self.cachePluginPortNum)
        self.emulator = interface.EmulatorClient(port=self.emulatorPortNum)
        self.injector = inj.FaultInjector(self.dirname)
        # injection values
        self.sectionToInject = s
        self.maxInjections = m
        self.numInjections = 0
        self.errorCountMatch = e
        # set up caches
        if usePlugin:
            self.injector.memHierarchy = mem.MemHierarchy(self.board)
        # other values
        self.regCls = benchmarks.getReg()
        self.injector.setRegCls(self.regCls)
        self.runtime = 0
        # flags
        self.exitStatus = 0
        self.dbgCom = False
        self.dbgEmu = False
        self.dbgSvr = False
        # super-duper debug commands
        self.dbgCmds = None

    def setDebug(self, dbgCommunicator, dbgEmulator, dbgServer, dbgCmds=None):
        """Set all the debug flags."""
        self.dbgCom = dbgCommunicator
        self.dbgEmu = dbgEmulator
        self.dbgSvr = dbgServer
        if self.emulator:
            self.emulator.setDebug(dbgEmulator)
        if self.emulatorServer:
            self.emulatorServer.setDebug(dbgEmulator)
        self.dbgCmds = dbgCmds
        if self.injector is not None:
            self.injector.dbgCmds = dbgCmds

    def setPrintQueue(self, q):
        self.outQ = q

    def printQueue(self, msg, log=True):
        """Send messages to be printed to the logging queue."""
        self.outQ.put(threads.LogQueueMessage(strings.qSrcSupervisor, msg, log))

    def getMMap(self):
        """Get the memory map of the ELF file."""
        self.mmap = utils.readElf(self.filename)

    def start(self, silent=False, doInject=False):
        """Start all of the subprocesses."""
        t0 = time.perf_counter()
        self.startEmulatorServer(doInject=doInject) # Emulator instance
        self.startEmulatorClient(silent=silent)     # talk to emulator over socket
        self.startGDBinstance(silent=silent)        # GDB client to connect to Emulator server
        t1 = time.perf_counter()
        if not silent:
            self.printQueue("Took {:.6f} seconds to start".format(t1 - t0))

    def stop(self, hard=False, silent=False):
        """Stop all of the subprocesses."""
        self.endEmulatorServer(hard, silent=silent)
        self.emulator.closePort()
        self.gdbSocket.close()

    # when you have to restart the subprocesses
    # called from in the GDB client loop
    def restart(self, hard=False, silent=False, doInject=False):
        """This handles if the simulation crashes before the campaign is done."""
        # cleanup first
        self.stop(hard, silent=silent)
        self.start(silent=silent, doInject=doInject)
        # we have to re-send some information here
        try:
            self.sendGDBcmd(strings.handlerSetupCmd)
            self.sendGDBcmds([strings.setTimeoutCmd, str(self.runtime)])
        except ConnectionError as e:
            print("Error supervisor.restart()", file=sys.stderr)
            raise e
        # continue in the client loop
        return

    def stopEmulatorOnly(self, hard=False, silent=False):
        self.endEmulatorServer(hard=hard, silent=silent)
        self.emulator.closePort()

    def startEmulatorOnly(self, doInject=False):
        self.startEmulatorServer(doInject=doInject)
        self.emulator.openPort()
        # self.sendGDBcmd(strings.disconnectCmd)
        time.sleep(0.01)
        self.sendGDBcmd(strings.reconnectCmd, response=False)

    def startEmulatorServer(self, doInject=False):
        if self.cachePluginPortNum is None:
            self.emulatorServer.start()
        else:
            self.emulatorServer.start(textStart=self.mmap.text.start,
                                      textEnd=self.mmap.init.start + self.mmap.init.size,
                                      doInject=doInject)

    def endEmulatorServer(self, hard=False, silent=False):
        """End the Emulator subprocess."""
        try:
            resp1 = self.emulator.quit()
            time.sleep(0.01)
            if resp1 is None:
                hard = True
        except socket.error as e:
            hard = True
            # print("Didn't need to stop Emulator?\n\t" + str(e), file=sys.stderr)
        self.emulatorServer.stop(hard, silent=silent)

    def startEmulatorClient(self, silent=False):
        """This subprocess communicates with the emulator over a telnet socket."""
        # set up the interface
        counter = 0
        while counter < 5:
            try:
                self.emulator.openPort()
            except ConnectionError:
                counter += 1
            else:
                break

        if not silent:
            self.printQueue("Starting Emulator client...")
        self.emulator.setupServer()

        self.emulatorServer.reloadExecutable()

    def endEmulatorClient(self):
        self.emulator.closePort()

    def sendGDBcmd(self, cmd, response=True):
        """Send a command to the GDB subprocess over a socket."""
        if self.gdbSocket is None:
            self.printQueue("Error, socket not set up yet!", False)
            return ""
        network.send_msg(self.gdbSocket, cmd.encode('utf-8'))
        if response:
            data = network.recv_msg(self.gdbSocket).decode('utf-8')
            return data
        else:
            return ""

    def sendGDBcmds(self, cmdList):
        """Send a sequence of commands to the GDB subprocess over a socket."""
        if self.gdbSocket is None:
            self.printQueue("Error, socket not set up yet!", False)
            return ""
        for c in cmdList:
            network.send_msg(self.gdbSocket, c.encode('utf-8'))
        data = network.recv_msg(self.gdbSocket).decode('utf-8')
        return data

    def sendGDBpickle(self, data):
        """Send pickled data to the GDB subprocess over a socket."""
        if self.gdbSocket is None:
            self.printQueue("Error, socket not set up yet!", False)
            return ""
        pdata = pickle.dumps(data)
        network.send_msg(self.gdbSocket, pdata)
        reply = network.recv_msg(self.gdbSocket).decode('utf-8')
        return reply

    def startGDBinstance(self, silent=False):
        """Start up the GDB instance as a subprocess."""
        # make sure don't have old one still running
        if self.gdb is not None:
            # poll() returning none means process is still going
            if self.gdb.poll() is None:
                # kill the old one before continuing
                self.endGDBinstance()
                time.sleep(0.01)

        # set up the GDB instance
        cmdStr = "{gdb} -x {script} -quiet --args {fname} {brd} {src} {gport} {pport} -n {max} -b {cnt} {dbg}".format(
            gdb=benchmarks.getGdbPath(),
            script=strings.gdbScriptName,
            fname=self.filename,
            max=self.maxInjections,
            cnt=self.numInjections,
            brd=self.board,
            src=self.dirname,
            gport=self.gdbServerPortNum,
            pport=self.gdbPythonPortNum,
            dbg='-c' if self.dbgSvr else '',
        )
        # debug
        if self.dbgSvr and not silent:
            print(" -> " + cmdStr)
        if not silent:
            self.printQueue("Starting GDB instance", False)
        self.gdb = sp.Popen(shlex.split(cmdStr), stdout=sp.PIPE, stderr=sys.stderr)

        # set up the socket
        self.gdbSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        time.sleep(0.1)

        # try to connect 5 times
        for t in range(1, 6):
            try:
                self.gdbSocket.connect((socket.gethostname(), self.gdbPythonPortNum))
                break
            except socket.error as e:
                if t == 5:
                    print("Error, could not connect to gdb socket", file=sys.stderr)
                else:
                    time.sleep(0.1*t)

        # send the configuration messages
        try:
            msg1 = network.recv_msg(self.gdbSocket).decode('utf-8')
            msg2 = network.recv_msg(self.gdbSocket).decode('utf-8')
        except ConnectionError as e:
            print("Error starting GDB instance", file=sys.stderr)
            # not really a good way to recover, but make sure other things exit nicely
            self.finish()
            raise e
        if not silent:
            self.printQueue(msg1, False)
            self.printQueue(msg2, False)

        # finishing configuring things
        try:
            if silent:
                self.sendGDBcmd(strings.silentConfigCmd)
            else:
                self.sendGDBcmd(strings.configGdbCmd)
        except ConnectionError as e:
            print("Error sending GDB commands in startGDBinstance()", file=sys.stderr)
            raise e
        self.injector.mmap = self.mmap
        self.injector.sectionToInject = self.sectionToInject

    def endGDBinstance(self):
        self.gdb.terminate()

    def finish(self):
        self.endGDBinstance()
        self.endEmulatorClient()


# use some "--" prefixed commands, but make them optional. This is nice,
#  because it means that argument order doesn't matter, as they're named.
def parseCommandLine():
    """Get configuration options from the command line."""
    parser = argparse.ArgumentParser(description="Supervisor for Emulator fault injection", formatter_class=MultilineFormatter)

    # absolutely required
    parser.add_argument('--filename', '-f', type=str, help="N|executable file to run.\nREQUIRED", required=True)
    parser.add_argument('--port-range', '-p', type=int, help="N|This number represents the beginning of a range of port numbers this instance will allocate. To leave a buffer space, it will reserve 5 ports, inclusive.\nREQUIRED", required=True)

    # configuration of fault injection campaign
    parser.add_argument('-t', metavar='N', type=int, default=1, help="number of injections")
    parser.add_argument('-e', '--errorCount', metavar='N', type=int, help="Run until 'N' number of errors seen, then complete the next 1000 injections.")
    parser.add_argument('--section', '-s', metavar='section', type=str, default='memory', help="memory section to inject faults into (or registers)", choices=['stack', 'text', 'rodata', 'data', 'bss', 'heap', 'init', 'registers', 'memory', 'cache', 'icache', 'dcache', 'l2cache'])
    parser.add_argument('--board', '-d', type=str, choices=['pynq', 'hifive1'], help="The platform the executable is intended to run on", default='pynq')

    # logging
    parser.add_argument('--log-dir', '-l', type=str, help="Directory in which to create the log files")
    parser.add_argument('--no-logging', '-q', action='store_true', help="Do not produce log files.")
    verbosityHelp = '''N|Select level of output verbosity. Choices:
n - normal (default)
c - debug communicator
e - print Emulator commands and responses
s - print GDB server commands
i - print Emulator INFO messages
a - all of the above
'''
    parser.add_argument('--verbosity', '-v', choices=['n', 'c', 'e', 's', 'i', 'a'], default='n', help=verbosityHelp)

    # debugging by re-creating certain conditions
    parser.add_argument('--forceBreak', '-b', metavar="EXPRESSION", type=str, help="ways to break simulation on purpose")
    parser.add_argument('--breakCount', '-c', metavar="ITERATION", type=int, help="when to break it", default=1)
    parser.add_argument('--breakSleep', '-z', metavar="TIME", type=float, help="how long to sleep before breaking it (float)", default=0.0)

    # super-duper debug mode!
    parser.add_argument('--debug-commands', '-x', metavar="FILENAME", type=str, help="path to file with GDB commands to execute right before injecting the fault")

    args = parser.parse_args()

    # validate the filename
    if not os.path.isfile(args.filename):
        print("Error, file {} does not exist!".format(args.filename), file=sys.stderr)
        sys.exit(-1)
    # validate log dir exists
    if args.log_dir:
        if not os.path.exists(args.log_dir):
            print("Error, directory {} does not exist!".format(args.log_dir),
                    file=sys.stderr)
            sys.exit(-1)
        elif not os.path.isdir(args.log_dir):
            print("Error, file path {} is not a directory!".format(args.log_dir),
                    file=sys.stderr)
            sys.exit(-1)
    # validate debug file path
    if args.debug_commands:
        if not os.path.exists(args.debug_commands):
            print("Path to file '{}' is invalid!".format(args.debug_commands), file=sys.stderr)
            sys.exit(-1)

    # make sure we have a valid port number
    usedPorts = [int(x.split(":")[-1]) for x in network.getTcpPorts()]
    prange = range(args.port_range, args.port_range+5)
    if (len(set(prange).intersection(set(usedPorts))) > 0) or (3333 in prange):
        print("Error, port(s) already in use!", file=sys.stderr)
        sys.exit(-1)

    if args.board == "hifive1":
        print("This board not yet supported in this version", file=sys.stderr)
        sys.exit(-1)

    return args


def main():
    # this will signal the end of the program
    finishedFlag = False

    # create objects
    args = parseCommandLine()
    if args.errorCount:
        eVal = args.errorCount
    else:
        eVal = None
    supervisor = Supervisor(
        args.t, args.section, args.filename,
        args.board, args.port_range, eVal
    )

    # shared objects
    logQ = Queue()
    uartQ = Queue()
    rrf = threading.Event()

    # set up log files
    if args.no_logging:
        unifiedLogFile = open("/dev/null", 'w')
        jsonLogFile = open("/dev/null", 'w')
    else:
        now = datetime.datetime.now()
        ts = "{yr}-{mo}-{d:02}_{h:02}-{m:02}".format(
            yr=now.year, mo=now.month, d=now.day,
            h=now.hour, m=now.minute
        )
        if args.log_dir:
            lfDir = args.log_dir
        else:
            lfDir = "./logs/"
        prefix = lfDir + supervisor.board + "_" + supervisor.dirname + "_" + ts
        unifiedLogFile = open(prefix + ".log", 'w')
        jsonLogFile = open(prefix + ".json", 'w', encoding='utf-8')
    jsonLogFile.write("{}\n[\n".format(args.filename))

    # handle all the logging
    dbgInfo = (args.verbosity in ['i', 'a'])
    supervisor.setPrintQueue(logQ)
    loggingThread = threading.Thread(target=threads.queueListener,
            args=(logQ, unifiedLogFile, jsonLogFile, rrf, dbgInfo),
            name="queueListener")
    loggingThread.start()

    # run the initialization sequence
    dbgCom = (args.verbosity in ['c', 'a'])
    dbgEmu = (args.verbosity in ['e', 'a'])
    dbgSvr = (args.verbosity in ['s', 'a'])
    # super-duper debug mode
    if args.debug_commands:
        with open(args.debug_commands, 'r') as dbgCmdFile:
            cmds = dbgCmdFile.readlines()
    else:
        cmds = None
    supervisor.setDebug(dbgCom, dbgEmu, dbgSvr, cmds)

    # start the supervisor
    supervisor.start()
    if args.forceBreak:
        supervisor.injector.setBreaking(args.forceBreak,
                                        args.breakCount,
                                        args.breakSleep)

    # start threads
    emulatorListenerThread = threading.Thread(target=threads.emuListener,
            args=(supervisor, logQ, uartQ, lambda: finishedFlag),
            name="emulatorListener")
    emulatorListenerThread.start()
    gdbListenerThread = threading.Thread(target=threads.gdbListener,
            args=(supervisor, logQ, lambda: finishedFlag),
            name="gdbListener")
    gdbListenerThread.start()
    gdbCommunicatorThread = threading.Thread(target=threads.gdbCommunicator,
            args=(supervisor, logQ, uartQ, rrf),
            name="gdbCommunicator")
    gdbCommunicatorThread.start()

    # log file header
    logQ.put(threads.LogQueueMessage(strings.qSrcSupervisor, "finished setting up objects", False))
    beginMessage = '\n' + utils.centerText("Beginning fault injection campaign", width=80)
    logQ.put(threads.LogQueueMessage(strings.qSrcSupervisor, beginMessage))
    fileMessage = utils.centerText("Filename: {}".format(args.filename), width=80, surround=' ')
    logQ.put(threads.LogQueueMessage(strings.qSrcSupervisor, fileMessage))
    configMessage = " Peforming {} injections into {}".format(args.t, args.section)
    if args.section == "all":
        configMessage += " sections"
    elif args.section != "registers":
        configMessage += " section"
    configMessage = utils.centerText(configMessage, width=80, surround=' ') + '\n'
    logQ.put(threads.LogQueueMessage(strings.qSrcSupervisor, configMessage))

    # now we block and let the threads run
    gdbCommunicatorThread.join()

    # when the client thread returns, then we're all done
    finishedFlag = True
    emulatorListenerThread.join()
    gdbListenerThread.join()

    # wrap up
    logQ.put("inferior process finished")
    logQ.put(strings.queueStopMsg)
    logQ.join()
    jsonLogFile.write("\n]\n")

    if not supervisor.exitStatus:
        supervisor.finish()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt as e:
        # how to call finish here?
        print("\b\bInterrupted")
        # this restores the terminal to its proper functioning state
        os.system("stty echo")
        # print timestamp so we can compare end to run logs
        os.system("date")
        try:
            sys.exit(0)
        except SystemExit:
            os._exit(0)
    # need this here too?
    os.system("stty echo")
    os.system("date")

# killing processes if they are left over
# ps aux | egrep "([s]upervisor.py|[g]db|[q]emu-system-)" | tee /dev/tty | awk '{print $2}' | xargs -r kill
