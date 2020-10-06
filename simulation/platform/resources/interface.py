# interface for starting and interacting with an Emulator instance
#  using TCP connection on localhost

import os
import sys
import time
import shlex
import signal
import socket
import subprocess as sp

import resources.utils as utils
import resources.network as network
import resources.benchmarks as benchmarks


RECV_TMOUT = 2


class EmulatorServer(object):
    """An interface for starting an Emulator Server which listens on a port."""
    def __init__(self, binary, gdbPort=3434, telnetPort=3435, pluginPort=None, useGDB=True, plfp=None):
        # filenames
        self.binary = binary
        self.emulatorScript = benchmarks.getScript()
        # port numbers
        self.gdbPort = gdbPort
        self.telnetPort = telnetPort
        # process handle
        self.handle = None
        # configuration parameters
        self.machine = benchmarks.getMachine()
        self.cpu = benchmarks.getCpu()
        self.memory = 512
        # flag
        self.debug = False
        # string for running the command
        self.runString = "{s} -semihosting --semihosting-config enable=on,target=native"
        self.runString += " -M {mach} -cpu {c} -nographic -kernel {k} -m {mem}M"
        if useGDB:
            self.runString += " -gdb tcp::{} -S".format(self.gdbPort)
        self.runString += " -monitor telnet::{t},server,nowait"
        # for using the cache/fault injection plugin
        self.pluginPort = pluginPort
        self.pluginSock = None
        self.socketResponse = None
        if pluginPort is not None:
            if pluginPort == "cache":
                # Use the plugin that gives cache characteristics
                self.pluginDir = os.path.abspath(os.path.join(os.path.abspath(__file__),
                        "../../../qemu-ccl/build/tests/plugin/libcache.so"))
                self.pluginStr = " -plugin {plug},arg={ts},arg={te} -d plugin -D {logFile}"
            else:
                # Use the plugin for fault injection
                self.pluginDir = os.path.abspath(os.path.join(os.path.abspath(__file__),
                        "../../../qemu-ccl/build/tests/plugin/libfaultinject.so"))
                self.pluginStr = " -plugin {plug},arg={ts},arg={te},arg={port},arg={host},arg={doInject} -d plugin"
        self.pluginLogFilePath = plfp

    def setPort(self, p):
        self.port = p

    def setDebug(self, flag):
        self.debug = flag

    def start(self, textStart=None, textEnd=None, doInject=False):
        """Opens a subprocess of the Emulator listening on a port."""
        cmd = self.runString.format(
            s=self.emulatorScript,
            mach=self.machine,
            c=self.cpu,
            k=self.binary,
            mem=self.memory,
            t=self.telnetPort
        )
        if self.pluginPort is not None:
            if self.pluginPort == "cache":
                cmd += self.pluginStr.format(
                    plug=self.pluginDir,
                    ts=hex(textStart),
                    te=hex(textEnd),
                    logFile=self.pluginLogFilePath
                )
            else:
                cmd += self.pluginStr.format(
                    plug=self.pluginDir,
                    ts=hex(textStart),
                    te=hex(textEnd),
                    port=str(self.pluginPort),
                    host=socket.gethostbyname(socket.gethostname()),
                    doInject="1" if doInject else "0",
                )
                # bind the socket now, so the client in qemu plugin can connect
                self.openSock()
            time.sleep(0.1)
        if self.debug:
            print("cmd = " + str(cmd))
        self.handle = sp.Popen(shlex.split(cmd), stdout=sp.PIPE, stderr=sp.STDOUT, start_new_session=True)
        # self.handle = sp.Popen(shlex.split(cmd), start_new_session=True)
        # give it some time to come up before we try to do anything with it
        # NOTE: in the past, this was as big as 1.5; may get bigger with multiple instances running
        time.sleep(0.1)
        if (self.pluginPort is not None) and (self.pluginPort != "cache"):
            self.connectSock()

    def openSock(self):
        self.pluginSock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.pluginSock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.pluginSock.bind((socket.gethostbyname(socket.gethostname()), self.pluginPort))
        self.pluginSock.settimeout(5)
        self.pluginSock.listen(0)

    def connectSock(self):
        # this should receive a connection from the plugin that was just loaded in start()
        self.socketResponse, address = self.pluginSock.accept()
        if self.debug:
            print("EmulatorServer - Accepted connection from {}".format(address))

    def setSockTimeout(self, timeout):
        self.socketResponse.settimeout(timeout)

    def sendMsg(self, msg):
        if self.socketResponse:
            network.send_msg(self.socketResponse, msg.encode('utf-8'))
        else:
            print("Error, plugin socket not yet set up!", file=sys.stderr)

    def recvMsg(self):
        if self.socketResponse:
            return network.recv_msg(self.socketResponse)
        else:
            print("Error, plugin socket not yet set up!", file=sys.stderr)
            return None

    def stop(self, hard=False, silent=False):
        """Terminate the subprocess."""
        if hard:
            pid = self.handle.pid
            if self.handle.poll() is None:
                pgrp = os.getpgid(pid)
                os.killpg(pgrp, signal.SIGINT)
        try:
            os.kill(self.handle.pid, 0)
            self.handle.terminate()
        except OSError:
            pass
        except Exception as e:
            raise e
        # make sure it worked
        time.sleep(0.01)
        if self.handle.poll() is None:
            if not silent:
                print("It's still not dead", file=sys.stderr)
            self.handle.kill()

    def reloadExecutable(self):
        # send the command "file [binary]"
        pass


class EmulatorClient(object):
    """An interface for interacting with an Emulator server running on a TCP port."""
    def __init__(self, host=None, port=3434):
        if host is not None:
            self.host = host
        else:
            self.host = socket.gethostname()
        self.port = port
        # handles
        self.sock = None
        self.debug = False
        # data
        self.boardScript = ""
        self.leftover = b''
        self.compare = bytes('\n', 'utf-8')
        # timeout signal
        signal.signal(signal.SIGALRM, handle_timeout)

    def setPort(self, p):
        self.port = p

    def setDebug(self, flag):
        self.debug = flag

    def setBoardScript(self, b):
        self.boardScript = b

    def openPort(self):
        """Set up the socket to connect to the server."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        count = 0
        timeout = 4

        while count <= timeout:
            try:
                self.sock.connect((self.host, self.port))
                break
            except socket.error as e:
                if count == timeout:
                    print(e)
                    print((self.host, self.port))
                    raise e
                else:
                    time.sleep(0.1)
                    count += 1
        self.sock.settimeout(1)

    def setupServer(self):
        """Gets the first printed line out of the output buffer."""
        if self.sock:
            self.recvLine()

    def closePort(self):
        """Close the port that connects to the server."""
        self.sock.close()

    def sendCmd(self, cmd):
        msg = bytes(cmd + '\n', 'utf-8')
        signal.alarm(5)
        try:
            self.sock.sendall(msg)
            # TODO: debug option for this in supervisor
            # print("$ emulator: " + cmd, file=sys.stderr)
        except TimeoutError:
            print("$ Timeout reached trying to send " + cmd, file=sys.stderr)
        except Exception as e:
            # r = "{}: sendCmd() -\n\tError, cmd \"{}\" not sent successfully: {}".format(utils.getFormattedTime(), cmd, e)
            # print(r)
            raise e
        finally:
            signal.alarm(0)

    def recvCmd(self, timeoutCnt=RECV_TMOUT):
        bufSize = 4096
        count = 0
        while count < timeoutCnt:
            try:
                msg = self.sock.recv(bufSize)
            except socket.timeout as e:
                err = e.args[0]
                if err == 'timed out':
                    count += 1
                    continue
                else:
                    # print("recvCmd() - Socket timed out: " + str(e))
                    return None
            except socket.error as e:
                # print("recvCmd() - Socket error: " + str(e))
                raise e
                return None
            except ConnectionError as e:
                print("The connection was reset")
                raise e
            else:
                if len(msg) == 0:
                    # print("server has shut down")
                    return None
                else:
                    return msg
            count += 1
        return None

    # The Emulator server sends back packets over the TCP socket,
    #  but they're not always in nice lines
    #  so these functions exist to get nice responses
    # It's important to always pair a sendCmd with a recvLine (or recvLines)
    #  so the responses don't get out of sync
    def recvLine(self, timeoutCnt=RECV_TMOUT):
        # check if the old data has a newline in it already
        if self.compare in self.leftover:
            dl = self.leftover.split(sep=self.compare, maxsplit=1)
            self.leftover = dl[1]
            ret = dl[0].strip(b'\r\n')
        else:
            # otherwise, get some new stuff
            data = self.recvCmd(timeoutCnt)
            if data is None:
                return data

            while not self.compare in data:
                nextData = self.recvCmd(timeoutCnt)
                if nextData is None:
                    return None
                else:
                    data += nextData
            dl = data.split(sep=self.compare, maxsplit=1)
            retStr = dl[0] + self.compare
            if self.leftover:
                retStr = self.leftover + retStr
            self.leftover = dl[1]

            ret = retStr.strip(b'\r\n')
        if self.debug:
            try:
                print("DBG: " + utils.stripHexCodes(ret.decode('utf-8')))
            except Exception:
                try:
                    print(ret)
                except:
                    pass
        return ret

    # recvLines tries hard to guarantee that the caller will get the exact
    #  number of lines it asked for, but if it it's taking too long, it will
    #  raise a ConnectionError exception
    def recvLines(self, lines=1, timeoutCnt=RECV_TMOUT):
        data = []
        count = 0

        for _ in range(lines):
            resp = self.recvLine(timeoutCnt)
            while resp is None:
                resp = self.recvLine(timeoutCnt)
                if count == timeoutCnt:
                    break
                count += 1
            data.append(resp)

        if (data is None) or (len(data) < lines):
            # return None
            raise ConnectionError
        return data

    # utility functions
    def quit(self):
        self.sendCmd("quit")
        return self.recvLine()

    def pause(self):
        self.sendCmd("stop")
        # need to give this some time to interrupt
        time.sleep(0.01)
        return self.recvLine()

    def start(self):
        self.sendCmd("start")
        return self.recvLine()


class TimeoutError(Exception):
    pass

def handle_timeout(signum, frame):
    import errno
    raise TimeoutError(os.strerror(errno.ETIME))
