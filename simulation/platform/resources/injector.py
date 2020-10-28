import os
import sys
import time
import random

import resources.utils as utils
import resources.strings as strings
from resources.supportClasses import (  InjectionLog,
                                        CacheInfo)
from resources.registers import nameLookup


class BreakInjection(object):
    """Collection of information about specific ways to break the fault injection."""
    def __init__(self, i=-1, a=None, v=None, s=None):
        # special value or -1 means it's inactive, -2 means always active
        self.iteration = i
        self.address = a
        self.value = v
        self.sleepTime = s


class FaultInjector(object):
    """FaultInjector class."""
    def __init__(self, d=""):
        # memory map
        self.sectionToInject = ""
        self.mmap = None
        # caches
        self.memHierarchy = None
        self.invalidRanges = [
            # f8f00200-f8f0021f (prio 0, i/o): a9gtimer shared
            range(int("0xf8f00200", 16), int("0xf8f0021f", 16)+1),
        ]
        # where is the file located
        self.dirname = d
        # handles
        self.emulatorServer = None
        self.regCls = None
        self.cmdQ = None
        self.msgQ = None
        # debugging information
        self.breaking = BreakInjection()
        self.dbgCmds = None
        # how long to wait for queues to respond
        # calling classes need to handle the exception
        self.qWaitTime = 10

    def setRegCls(self, c):
        """Set the class of registers being injected into."""
        self.regCls = c

    def setQueues(self, msgQ, cmdQ):
        """Queues to talk with GDB."""
        self.msgQ = msgQ
        self.cmdQ = cmdQ

    # cmd should be of the form "set [addr] = [val]"
    def setBreaking(self, cmd, c, s):
        """Force specific fault injection."""
        cmds = cmd.split()
        if len(cmds) == 4:
            self.breaking.iteration = c
            self.breaking.address = cmds[1]
            self.breaking.value = cmds[3]
            self.breaking.sleepTime = s
        else:
            print("Invalid format for break command", file=sys.stderr)

    def getReg(self):
        """get a random register to inject a fault into."""
        return random.choice(list(self.regCls))

    def pluginCommunicate(self, disp=False):
        """Communicates with the QEMU plugin to read cache address and timing.

        We have to be careful that the breakpoint isn't hit right before this
        communication happens. If it does, then the code will hang forever waiting
        for a response, which can't be sent, since the plugin can't operate while
        QEMU is stopped at a breakpoint.
        """
        cacheName, row, block, word = self.memHierarchy.randomWordCacheAddr(self.sectionToInject)
        if disp:
            print("Injecting in {}: {}, {}, {}".format(cacheName, row, block, word))
        self.emulatorServer.sendMsg(str(row))
        self.emulatorServer.sendMsg(str(block))
        self.emulatorServer.sendMsg(cacheName)
        try:
            # try this for maxUartWait seconds
            validness = bool(int(self.emulatorServer.recvMsg()))
        except ConnectionError:
            # if the above hangs, then see if a breakpoint was hit
            pendingMsg = self.msgQ.get(timeout=self.qWaitTime)
            if pendingMsg == strings.bpMsg:
                # pop place message
                self.msgQ.get(timeout=self.qWaitTime)
                if True: # disp
                    print("Hit bp while communicating with plugin")
                # tell it to continue
                self.cmdQ.put(strings.contCmd)
                # get the validness again
                validness = bool(int(self.emulatorServer.recvMsg()))
            else:
                raise ValueError(pendingMsg)
        self.emulatorServer.sendMsg(str(word))

        actualCycles = int(self.emulatorServer.recvMsg(), 16)
        randAddr = int(self.emulatorServer.recvMsg(), 16)
        if disp:
            print("Actual cycles = {}".format(actualCycles))
            print("Received address {}".format(randAddr))

        # have to wait
        self.cmdQ.put(strings.interruptCmd)
        stopMsg = self.msgQ.get(timeout=self.qWaitTime)  # stop message
        # for some reason, this seems to trigger a breakpoint message
        #  if the PC is too close to the BP when we interrupt
        if stopMsg == strings.bpMsg:
            self.msgQ.get(timeout=self.qWaitTime)

        return (actualCycles,
                CacheInfo(cacheName, row, block, word, dirty=(not validness)),
                randAddr)

    def injectFault(self, numInjections, disp=False, pluginFlag=False):
        if pluginFlag:
            # get some stuff first
            actualCycles, cacheInfo, cacheAddr = self.pluginCommunicate(disp)

        # force certain condition
        if (numInjections == self.breaking.iteration) or \
                (self.breaking.iteration == -2):

            # before injecting fault, check for super-debug commands
            if isinstance(self.dbgCmds, list):
                print("Super duper debug mode:")
                for cmd in self.dbgCmds:
                    # skip empty lines
                    if (not cmd.isspace()) and (not cmd.startswith("#")):
                        self.execGDBcmd(cmd.rstrip())

            # inject a fault
            reg = nameLookup(self.regCls, self.breaking.address)
            if reg is None:
                # then injecting into memory
                randAddr = int(self.breaking.address, base=16)
                sectionName = 'memory'
                oldVal, newVal, memName = self.injectFaultMem(randAddr,
                        overwriteVal=self.breaking.value)
            else:
                randAddr = reg.name
                sectionName = 'registers'
                oldVal, newVal, memName = self.injectFaultReg(reg,
                        overwriteVal=self.breaking.value)

        # cache injection
        elif "cache" in self.sectionToInject:
            oldVal, newVal, memName = self.injectFaultMem(cacheAddr)
            sectionName = self.sectionToInject
            randAddr = cacheAddr

        # injecting into registers
        elif self.sectionToInject == 'registers':
            sectionName = 'registers'
            randReg = self.getReg()
            oldVal, newVal, memName = self.injectFaultReg(randReg)
            randAddr = randReg.name
        # injecting into memory
        else:
            # sometimes we have to pick a random section
            if self.sectionToInject == 'memory':
                section = self.mmap.getRandomSection()
            else:
                section = self.mmap.list[self.sectionToInject]
            sectionName = section.name
            # don't do special stuff with the stack pointer like before

            randAddr = section.getRandomAddress()
            oldVal, newVal, memName = self.injectFaultMem(randAddr)

        # current time
        dateStr = utils.getFormattedTime()

        # return fault injection result
        il = InjectionLog(
            it=dateStr,
            num=numInjections,
            s=sectionName,
            adr=randAddr,
            old=oldVal,
            new=newVal,
            name=memName
        )
        if "cache" in self.sectionToInject:
            il.cacheInfo = cacheInfo

        if pluginFlag:
            return il, actualCycles
        else:
            return il

    def flipOneBit(self, val, bitlen=32):
        """Flips one bit of a 32 bit number."""
        randomBit = random.randint(0, bitlen-1)
        bitMask = 0x01 << randomBit
        newVal = bitMask ^ val
        return newVal

    def injectFaultMem(self, addr, overwriteVal=None):
        # there are some areas where we aren't allowed to inject
        if any([int(addr) in r for r in self.invalidRanges]):
            raise ValueError(strings.invalidRange)
        # read current value
        self.cmdQ.put([strings.readMemCmd, addr])
        getVal = self.msgQ.get(timeout=self.qWaitTime)
        try:
            memVal = int(getVal, base=16)
        except ValueError:
            raise ValueError(getVal)

        # get the name
        self.cmdQ.put([strings.memNameCmd, addr])
        memName = self.msgQ.get(timeout=self.qWaitTime)

        # make it possible to force a certain value
        if overwriteVal is not None:
            newMemVal = overwriteVal
        else:
            # flip one bit
            newMemVal = self.flipOneBit(memVal)

        # write new value
        self.cmdQ.put([strings.writeMemCmd, addr, newMemVal])

        return memVal, newMemVal, memName

    def injectFaultReg(self, reg, overwriteVal=None):
        # get the current register value
        self.cmdQ.put([strings.readRegCmd, reg.name])
        getVal = self.msgQ.get(timeout=self.qWaitTime)
        try:
            regVal = int(getVal, base=16)
        except ValueError:
            raise ValueError(getVal)

        # get the name
        self.cmdQ.put([strings.regNameCmd, reg.name])
        memName = self.msgQ.get(timeout=self.qWaitTime)

        # make it possible to force a certain value
        if overwriteVal is not None:
            newRegVal = overwriteVal
        else:
            # flip one bit
            newRegVal = self.flipOneBit(regVal)

        # write new value
        self.cmdQ.put([strings.writeRegCmd, reg.name, newRegVal])

        return regVal, newRegVal, memName

    def execGDBcmd(self, cmd):
        # print("Executing command `{}`:".format(cmd))
        print(" - `{}`".format(cmd))
        self.cmdQ.put([strings.gdbExecCmd, cmd])
        retVal = self.msgQ.get(timeout=self.qWaitTime)
        # print("Response:\n{}".format(retVal.rstrip()))
        print("{}".format(retVal.rstrip()))
