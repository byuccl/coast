import gdb
import sys
import traceback
from queue import Empty

# if we want to get values back from gdb when using post_event,
#  we need to hava a queue
responseQueueHandle = None

def setResponseQueue(rqh):
    global responseQueueHandle
    responseQueueHandle = rqh


class Executor(object):
    """Useful in posting events to GDB.
    
    @param p - print response
    @param t - interactive mode
    @param d - debug print
    """
    def __init__(self, cmd, p=False, t=False, d=False, i=False):
        self.__cmd = cmd
        self.getStr = p
        self.interactive = t
        self.debug = d
        self.ignore = i

    def __call__(self):
        if self.debug:
            print("execute: " + self.__cmd, file=sys.stderr)
        try:
            outputStr = gdb.execute(self.__cmd, to_string=self.getStr, from_tty=self.interactive)
            if self.getStr:
                responseQueueHandle.put(outputStr)
        except (gdb.GdbError, gdb.error) as e:
            if not self.ignore:
                print(e)
                print("Command: " + str(self.__cmd))
                traceback.print_exc()
            elif self.ignore and self.getStr:
                # if any error happens, and we're collecting output, return it
                outputStr = str(e) + "\n"
                outputStr += "Command: " + str(self.__cmd)
                # traceback prints straight to stderr?
                # outputStr += str(traceback.print_exc())
                responseQueueHandle.put(outputStr)
            # try:
            #     # give some context as to where it broke
            #     print(gdb.execute("i r pc", to_string=True))
            #     print(gdb.execute("list", to_string=True))
            # except:
            #     pass


def continueDebug(count=1):
    """Wrapper to GDB "continue" command."""
    cmdStr = "continue"
    if count > 1:
        cmdStr += " {}".format(count)
    gdb.post_event(Executor(cmdStr))


def getVariable(varname):
    """Read a variable using GDB interface"""
    gdb.post_event(Executor("p {}".format(varname), p=True))
    try:
        readVal = responseQueueHandle.get(timeout=2)
    except Empty:
        return "error reading variable {}".format(varname)
    return readVal.split(" = ")[-1].rstrip()

def parseSymbolInfo(strVal):
    """Parse the output of the command `info symbol [addr|reg]`."""
    if strVal.startswith("No symbol"):
        memName = None
    else:
        memInfo = strVal.split()
        memName = memInfo[0]
        if memInfo[1] == "+":
            memName += " {} {}".format(*memInfo[1:3])

    return memName


def readReg(regName):
    """Returns the value of a register."""
    gdb.post_event(Executor("i r {}".format(regName), p=True))
    readVal = responseQueueHandle.get()
    # returns the hex string
    if "raw" in readVal:
        # it's a floating point register and must be parsed differently
        regVal = readVal.split()[-1][:-1].strip()
    else:
        regVal = readVal.split()[-2].rstrip()

    return regVal

def getNameReg(regName):
    """Returns the name of the symbol contained in a register, if any."""
    # get information about what variables are being changed
    gdb.post_event(Executor("info symbol ${}".format(regName), p=True))
    readVal = responseQueueHandle.get()
    memName = parseSymbolInfo(readVal)
    return memName

def writeReg(regName, regVal):
    """Sets a register to a value."""
    gdb.post_event(Executor("set ${} = {}".format(regName, regVal)))


def readMem(addr):
    """Returns the value at a given memory address."""
    gdb.post_event(Executor("x/wx {}".format(addr), p=True))
    readVal = responseQueueHandle.get()
    # hex string
    memVal = readVal.split(":")[-1].rstrip()

    return memVal

def getNameMem(addr):
    """Returns the name of the symbol at the given memory address, if any."""
    # return information about what variable(s) (if any) are being changed
    gdb.post_event(Executor("info symbol {}".format(addr), p=True))
    readVal = responseQueueHandle.get()
    memName = parseSymbolInfo(readVal)
    return memName

def getSymAddr(name):
    """Returns the address of the symbol."""
    gdb.post_event(Executor("info address {}".format(name), p=True))
    readVal = responseQueueHandle.get()
    # if the name exists, the address will be the last thing, in hex
    lastElem = readVal.replace(".", "").split()[-1]
    try:
        memAddr = int(lastElem, 16)
    except ValueError:
        memAddr = None
    return memAddr

def writeMem(addr, memVal):
    """Set the value of a memory address."""
    gdb.post_event(Executor("set *{} = {}".format(addr, memVal)))

def execCmd(cmdStr):
    gdb.post_event(Executor(cmdStr, p=True, i=True))
    retVal = responseQueueHandle.get()
    return retVal
