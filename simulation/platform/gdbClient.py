#!/usr/bin/python3

"""Main script that will be run by the GDB instance."""

import os
import re
import gdb
import sys
import time
import shlex
import signal
import socket
import argparse
import threading
import faulthandler
from math import ceil
from queue import Queue

# import all of the submodules
sys.path.append(os.path.realpath(os.getcwd()))
import resources.utils as utils
import resources.strings as strings
import resources.network as network
import resources.benchmarks as benchmarks
import resources.gdbCommands as gdbCommands
import resources.gdbHandlers as gdbHandlers


# globals
LOG_FILE='/tmp/gdb.secondary.log'


class GDBserver(object):
    """This class is a wrapper around a running instance of GDB.

    It implements methods that allow it to be controlled over a
    socket using commands found in the file strings.py.
    """
    def __init__(self, c, m, b, d, f):
        # handles
        self.socketResponse = None
        self.serverThread = None
        self.bpLoc = None
        # queue for getting printing back
        self.cmdRespQ = Queue()
        gdbCommands.setResponseQueue(self.cmdRespQ)
        # scalar values
        self.timeout = 5
        self.timingRun = 0
        self.injectionCount = c
        self.maxInjections = m
        # ports
        self.gdbPortNum = 0
        self.pythonPortNum = 0
        # flags
        self.stopThreads = False
        self.finished = False
        self.debug = False
        self.silent = False
        # configuration parameters
        self.board = b
        self.dirname = d
        self.fileName = f
        self.resetPCval = "0x100000"
        # assuming this must be done because running inside a separate python instance
        if not benchmarks.setBoardBenchmark(self.board, self.dirname):
            print("Error setting up benchmark data!", file=sys.stderr)

    # this doesn't seem to be working
    def disconnect(self):
        gdb.post_event(gdbCommands.Executor("disconnect"))

    # this doesn't seem to be working
    def reconnect(self):
        time.sleep(0.01)
        # first one is to trigger an error and get it to disconnect, so we ignore error output
        gdb.post_event(gdbCommands.Executor("target remote :{}".format(self.gdbPortNum), i=True))
        gdb.post_event(gdbCommands.Executor("target remote :{}".format(self.gdbPortNum)))
        # gdb.execute("target remote :{}".format(self.gdbPortNum))

    def setPorts(self, g, p):
        """Socket port parameters."""
        self.gdbPortNum = g
        self.pythonPortNum = p

    def cleanup(self, died=False):
        """This is the last function that should run before the program closes"""
        # pause the target
        if not self.silent:
            print("Cleaning up program...")
        self.stopThreads = True
        try:
            gdb.events.stop.disconnect(gdbHandlers.stop_handler)
            gdb.events.cont.disconnect(gdbHandlers.cont_handler)
            gdb.events.exited.disconnect(gdbHandlers.exited_handler)
            if not died:
                gdb.post_event(gdbCommands.Executor("interrupt"))
                gdb.post_event(gdbCommands.Executor("disconnect"))
            gdb.post_event(gdbCommands.Executor("set confirm off"))
            gdb.post_event(gdbCommands.Executor("quit"))
        except (gdb.GdbError, gdb.error) as e:
            if not self.silent:
                print(e)
        if not self.silent:
            print("Done!\n")
        self.sendResponseMsg(strings.finishedMsg)
        time.sleep(0.01)
        return

    def sendResponseMsg(self, msg):
        """Sends a message back to the GDB controller over the open socket"""
        if msg is None:
            msg = "SERVER ERROR: tried to send null response"

        if self.debug:
            print("GDB PRINT: " + msg)

        if self.socketResponse:
            network.send_msg(self.socketResponse, msg.encode('utf-8'))
            # sleeping makes sure we don't receive 2 packets
            #  as the same one on the other end
            time.sleep(0.01)
        else:
            # error checking
            print("Error, socket not yet set up!", file=sys.stderr)

    def reloadExecutable(self):
        """Reloads the ELF file and resets the PC."""
        gdb.post_event(gdbCommands.Executor("file {}".format(self.fileName)))
        gdb.post_event(gdbCommands.Executor("set $pc = {}".format(self.resetPCval)))

    def readGlobalTimer(self):
        """Read the value of the Zynq A9 global timer."""
        gdb.post_event(gdbCommands.Executor("call (void)XTime_GetTime(&{})".format(strings.tProbeName)))
        timerVal = gdbCommands.getVariable(strings.tProbeName)
        return timerVal

    def configure(self):
        """Configures the GDB instance and sets up the breakpoint."""
        gdb.execute("set logging file {}".format(LOG_FILE))
        gdb.execute("set pagination off")
        gdb.execute("set print pretty")
        # connect to remote target
        gdb.execute("target remote :{}".format(self.gdbPortNum), to_string=True)
        # suppress writing to screen
        gdb.execute("monitor pause", to_string=True)
        # date the log file
        with open(LOG_FILE, 'a') as tempFile:
            tempFile.write(" ===== {} ===== \n".format(utils.getFormattedTime()))
        gdb.execute("set logging on")
        gdb.execute("set scheduler-locking off")
        if not self.silent:
            print("GDB setup complete!\n")

        if self.dirname == "":
            print("Error setting up values!", file=sys.stderr)
            return -1

        # set up silent breakpoints
        bpLocs = benchmarks.getBpLocation()
        self.bpLoc = []
        # constructor guarantees it is a list
        for bp in bpLocs:
            tmpBp = gdb.Breakpoint(bp)
            tmpBp.silent = True
            self.bpLoc.append(tmpBp)

        # what is the reset PC value?
        self.resetPCval = gdbCommands.readReg("pc")

        # connect a handler
        gdb.events.stop.connect(gdbHandlers.simple_stop_handler)
        return 0

    def setUpHandlers(self):
        """Registers the event handlers"""
        gdb.events.stop.disconnect(gdbHandlers.simple_stop_handler)
        gdb.events.exited.connect(gdbHandlers.exited_handler)
        gdb.events.stop.connect(gdbHandlers.stop_handler)
        gdb.events.cont.connect(gdbHandlers.cont_handler)

    def setTimeout(self, t):
        """This is how long the watchdog will wait before triggering."""
        bufferMultiplier = 1.2
        # round up to the nearest second, buffer
        self.timeout = int(ceil(t * bufferMultiplier))
        if self.debug:
            print("Setting timeout to " + str(self.timeout))

    def setDebug(self, d):
        self.debug = d

    def socketServer(self):
        """Handle all of the commands that come to the GDB instance.

        This function will be run in a separate thread so that the GDB instance
        running underneath will be able to execute commands.
        """

        # set up socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind((socket.gethostname(), self.pythonPortNum))
        except OSError as oe:
            print("Failed binding with error number {}".format(oe.errno))
            # Since this socket was how we were going to communicate with the supervisor,
            #  just return here and wait for the supervisor to notice the timeout
            return
        sock.settimeout(5)
        sock.listen(1)

        # try to connect 5 times
        for t in range(1, 6):
            try:
                self.socketResponse, address = sock.accept()
                break
            except socket.timeout:
                if t == 5:
                    print("Error, could not accept socket connection in GDB socket server",
                            file=sys.stderr)
                else:
                    time.sleep(0.1*t)

        sendStr = "Opening socket on port " + str(sock.getsockname())
        self.sendResponseMsg(sendStr)
        sendStr = "Connection from: " + str(address)
        self.sendResponseMsg(sendStr)

        # service commands sent
        msg = ""
        while True:
            try:
                data = network.recv_msg(self.socketResponse)
                data = data.decode('utf-8')
            except ConnectionError:
                print("GDB client side socket error!", file=sys.stderr)
            except AttributeError as ae:
                print(ae)
                print("Could not decode: `{}`".format(data))
                continue
            if not data:
                print("GDB Error: Ran out of data to decode!")
                break

            # debug printing
            if self.debug:
                print("SERVER: " + data, file=sys.stderr)

            ##################### configuring ######################
            if data == strings.configGdbCmd:
                self.configure()
                msg = strings.configGdbMsg
            elif data == strings.silentConfigCmd:
                self.silent = True
                self.configure()
                self.silent = False

            elif data == strings.setTimeoutCmd:
                runtime = network.recv_msg(self.socketResponse).decode('utf-8')
                self.setTimeout(float(runtime))
                msg = strings.setTimeoutMsg
            elif data == strings.handlerSetupCmd:
                msg = strings.handlerSetupMsg
                self.setUpHandlers()

            # talking to the Qemu instance
            elif data == strings.disconnectCmd:
                self.disconnect()
                continue
            elif data == strings.reconnectCmd:
                self.reconnect()
                continue
            
            ##################### control flow #####################
            elif data.startswith(strings.contCmd):
                if not data == strings.contCmd:
                    try:
                        contCount = int(data.split(maxsplit=1)[1])
                    except (ValueError, TypeError):
                        contCount = 1
                else:
                    contCount = 1
                gdbCommands.continueDebug(contCount)
                # no response
                continue
            elif data == strings.quitCmd:
                self.sendResponseMsg(strings.quitMsg)
                self.finished = True
                self.cleanup()
                break
            elif data == strings.killGdbCmd:
                self.sendResponseMsg(strings.killGdbMsg)
                self.cleanup(True)
                break
            elif data == strings.silentKillCmd:
                self.sendResponseMsg(strings.killGdbMsg)
                self.silent = True
                self.cleanup(True)
                break
            elif data == strings.reloadCmd:
                self.reloadExecutable()
                # no response
                continue
            elif data == strings.interruptCmd:
                gdb.post_event(gdbCommands.Executor("interrupt", p=True))
                print(self.cmdRespQ.get())
                time.sleep(0.01)
                # get the string to make sure it completes before 
                #  continuing, but don't return it
                continue
            
            ##################### read values ######################
            # variables
            elif data == strings.getVarCmd:
                varName = network.recv_msg(self.socketResponse).decode('utf-8')
                if self.debug:
                    print(" reading {}".format(varName))
                msg = gdbCommands.getVariable(varName)
            elif data == strings.readGlblTimerCmd:
                msg = self.readGlobalTimer()

            # registers
            elif data == strings.readRegCmd:
                regName = network.recv_msg(self.socketResponse).decode('utf-8')
                if self.debug:
                    print(" reading {}".format(regName))
                regVal = gdbCommands.readReg(regName)
                msg = regVal
            elif data == strings.readMemCmd:
                addr = network.recv_msg(self.socketResponse).decode('utf-8')
                if self.debug:
                    print(" reading {}".format(addr))
                # return the value and name of the variable being read
                memVal = gdbCommands.readMem(addr)
                msg = memVal

            # memory addresses
            elif data == strings.regNameCmd:
                regName = network.recv_msg(self.socketResponse).decode('utf-8')
                memName = gdbCommands.getNameReg(regName)
                msg = memName if memName is not None else "None"
            elif data == strings.memNameCmd:
                addr = network.recv_msg(self.socketResponse).decode('utf-8')
                memName = gdbCommands.getNameMem(addr)
                msg = memName if memName is not None else "None"
            elif data == strings.symbolAddrCmd:
                name = network.recv_msg(self.socketResponse).decode('utf-8')
                memAddr = gdbCommands.getSymAddr(name)
                msg = str(memAddr) if memAddr is not None else "None"

            # arbitrary command
            elif data == strings.gdbExecCmd:
                # other side responsible for decoding the information
                cmdStr = network.recv_msg(self.socketResponse).decode('utf-8')
                msg = gdbCommands.execCmd(cmdStr)
            
            ##################### write values #####################
            elif data == strings.writeRegCmd:
                regName = network.recv_msg(self.socketResponse).decode('utf-8')
                regVal = network.recv_msg(self.socketResponse).decode('utf-8')
                gdbCommands.writeReg(regName, regVal)
                continue
            elif data == strings.writeMemCmd:
                addr = network.recv_msg(self.socketResponse).decode('utf-8')
                memVal = network.recv_msg(self.socketResponse).decode('utf-8')
                gdbCommands.writeMem(addr, memVal)
                continue
            # otherwise, unrecognized command
            else:
                msg = "invalid command: " + data
            self.sendResponseMsg(msg)

        if not self.silent:
            print("GDB instance closed")


    def run(self):
        self.serverThread = threading.Thread(target=self.socketServer, args=(), name="GDB socket server")
        self.serverThread.start()

        # set up signal handler for manual debugging
        # we have to set it to output to a different file because
        #  stderr and stdout work differently inside of GDB
        recordErrorsFile = open('errors_gdbClient.log', 'a')
        faulthandler.enable(file=recordErrorsFile, all_threads=True)
        faulthandler.register(signal.SIGUSR1, file=recordErrorsFile, all_threads=True)


def parseCommandLine():
    """Get configuration options from the command line."""
    # GDB puts extra args in quotes when you ask for them
    showArgs = gdb.execute("show args", to_string=True)
    CLargs = shlex.split(re.findall(r'"(.*?)"', showArgs)[0])
    parser = argparse.ArgumentParser(description="If you can read this, you're too close")
    parser.add_argument('board', type=str, help="Target architecture")
    parser.add_argument('source_dir', metavar='source-dir', type=str, help="directory containing source file")
    parser.add_argument('gdb_port', metavar='gdb-port', type=int, help="socket number GDB server is running on")
    parser.add_argument('python_port', metavar='python-port', type=int, help="socket number to host Python server on")
    parser.add_argument('--num-injections', '-n', type=int, help="total number of injections", default=2)
    parser.add_argument('--start-num', '-b', type=int, help="number to start counting at", default=0)
    parser.add_argument('--debug-commands', '-c', action='store_true', default=False)
    args = parser.parse_args(CLargs)
    return args


def main():
    args = parseCommandLine()
    fileName = gdb.current_progspace().filename
    server = GDBserver(args.start_num, args.num_injections, args.board, args.source_dir, fileName)
    server.setPorts(args.gdb_port, args.python_port)
    server.setDebug(args.debug_commands)
    gdbHandlers.setServer(server)
    server.run()
    # now that this "finishes", and we have a bunch of background threads running,
    #  this main thread will let GDB run whatever commands we give it


main()
