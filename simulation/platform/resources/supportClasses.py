# many of the support classes for the fault injector

from enum import Enum

import resources.utils as utils


class LogQueueMessage(object):
    """The things that are passed into the logging queue."""
    def __init__(self, src, msg, l=True):
        self.src = src
        self.msg = msg
        self.log = l


class UartQueueMsg(object):
    """The things that are passed into the queue of UART output."""
    def __init__(self, msg, data):
        self.msg = msg
        self.data = data


class ErrorMessage:
    def __init__(self, msg):
        self.msg = msg


class InfoMessage:
    def __init__(self, msg):
        self.msg = msg


class CommonResult(object):
    """Common class for all Result type objects."""
    def __init__(self, ft=None):
        """Pretty much just takes care of the time stamp."""
        if ft is None:
            ft = utils.getFormattedTime()
        self.ftime = ft


class CacheInfo:
    """Information about where in the cache the memory address came from."""
    def __init__(self, name, row, block, word, tag=False, dirty=True):
        self.name = name
        self.row = row
        self.block = block
        self.word = word
        self.inTag = tag
        self.dirty = dirty      # most memory has dirty bit set by default

    def __str__(self):
        outStr = "{}: {}, {}, {}".format(
            self.name, self.row, self.block, self.word
        )
        if self.inTag:
            outStr += " (tag)"
        return outStr

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "name"      : self.name,
            "row"       : self.row,
            "block"     : self.block,
            "word"      : self.word,
            "inTag"     : self.inTag,
            "dirty"     : self.dirty,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        ci = CacheInfo(
            d['name'],
            d['row'],
            d['block'],
            d['word'],
            d['inTag'],
            d['dirty'],
        )
        return ci


class InvalidResult(CommonResult):
    """Contains the result of a run that printed invalid output."""
    def __init__(self, s, ft=None):
        super().__init__(ft)
        self.string = s

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "invalid"   : self.string,
            "timestamp" : self.ftime,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        ir = InvalidResult(d["invalid"], d["timestamp"])
        return ir

    def __str__(self):
        return self.ftime + " " + self.string


class AssertionFailResult(CommonResult):
    """Contains the result of a run that ended with an assertion fail."""
    def __init__(self, fl, ln, ft=None):
        super().__init__(ft)
        self.file = fl
        self.line = ln
        self.errors = 1     # this exists so it can be parsed correctly

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "file"      : self.file,
            "line"      : self.line,
            "timestamp" : self.ftime,
            "errors"    : self.errors,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        afr = AssertionFailResult(
            d['file'],
            d['line'],
            d['timestamp'],
        )
        return afr

    def __str__(self):
        return "{} Assertion failed in file {}, line {}".format(
                self.ftime, self.file, self.line)


class AbortResult(CommonResult):
    """Contains the result of a run that ended with an abort."""
    def __init__(self, ty, msg, ft=None):
        super().__init__(ft)
        self.type = ty
        self.message = msg
        self.errors = 1     # this exists so it can be parsed correctly

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "type"      : self.type,
            "message"   : self.message,
            "timestamp" : self.ftime,
            "errors"    : self.errors,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        ar = AbortResult(
            d['type'],
            d['message'],
            d['timestamp'],
        )
        return ar

    def __str__(self):
        return "{} {} abort with {}".format(
                self.ftime, self.type, self.message)


class StackOverflowResult(CommonResult):
    """Contains the result of a run that reported a stack overflow in a task."""
    def __init__(self, tsk, ft=None):
        super().__init__(ft)
        self.task = tsk
        self.errors = 1     # this exists so it can be parsed correctly

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "task"      : self.task,
            "timestamp" : self.ftime,
            "errors"    : self.errors,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        sor = StackOverflowResult(
            d['task'],
            d['timestamp'],
        )
        return sor

    def __str__(self):
        return "{} HALT: Task {} overflowed its stack.".format(
                self.ftime, self.task)


class TimeoutResult(CommonResult):
    """Contains the result of a run that timed out."""
    def __init__(self, s, ft=None):
        super().__init__(ft)
        self.string = s
        self.trap = False

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        tr = TimeoutResult(d["timeout"], d["timestamp"])
        tr.trap = d["trap"]
        return tr

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "trap"      : self.trap,
            "timeout"   : self.string,
            "timestamp" : self.ftime,
        }

    def __str__(self):
        return self.ftime + " " + self.string


class RunResult(CommonResult):
    """Contains the result of a run that printed correctly."""
    def __init__(self, cr, er, flt, rt):
        super().__init__()
        self.core = cr
        self.errors = er
        self.faults = flt
        self.runTime = rt

    def __str__(self):
        outStr = "{ftime} Done. Core: {cr} Errors: {er} Faults: {flt} Runtime: {rt:.6f}".format(
            ftime=self.ftime,
            cr=self.core,
            er=self.errors,
            flt=self.faults,
            rt=self.runTime,
        )
        return outStr

    def isSuccess(self):
        return (self.errors == 0) and (self.faults == 0)

    def hasError(self):
        return self.errors != 0

    def hasFault(self):
        return self.faults != 0

    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "timestamp" : self.ftime,
            "core"      : self.core,
            "runtime"   : self.runTime,
            "errors"    : self.errors,
            "faults"    : self.faults,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        rr = RunResult(
            d["core"],
            d["errors"],
            d["faults"],
            d["runtime"]
        )
        rr.ftime = d["timestamp"]
        return rr


class InjectionLog(object):
    """Data about a fault injected."""
    def __init__(self, it, num, s, adr, old, new, name="None"):
        self.injectionTime = it     # timestamp
        self.number = num           # number 0 through N-1
        self.section = s            # memory section or registers
        self.address = adr          # address or register name
        self.oldValue = old         # previous value
        self.newValue = new         # value after injection
        self.sleepTime = 0          # how long it slept before injecting
        self.cycles = 0             # cycles since starting the program
        self.pcVal = 0              # value of the program counter
        self.name = name            # name of the symbol at the location being injected
        self.result = None          # object that inherits from CommonResult
        self.cacheInfo = None       # where in the cache - only applies if section is "cache"

    def addInjectionInfo(self, st, cy, pc):
        self.sleepTime = st
        self.cycles = cy
        self.pcVal = pc

    def addRunLog(self, r):
        self.result = r

    @staticmethod
    def parseNewVal(nv):
        """Parses string returned from injector and returns tuple of address, value."""
        parsed = nv.split(' = ')
        addr = parsed[0].split()[1]
        val = parsed[1]
        return (addr, val)

    def whenStr(self):
        outStr = "\tPC @ {}"
        if self.sleepTime:
            outStr += ", slept for {:.10f} seconds"
        outStr += ", ran for {} cycles"
        if self.sleepTime:
            return outStr.format(self.pcVal, self.sleepTime, self.cycles)
        else:
            return outStr.format(self.pcVal, self.cycles)

    def whatStr(self):
        outStr = "{} Injection #{}: set {} = {:10}".format(
            self.injectionTime, self.number, self.address, self.newValue
        )
        if self.name == "None":
            outStr += " ({})".format(self.section)
        else:
            outStr += " ({}: {})".format(self.section, self.name)
        return outStr

    def __str__(self):
        outStr = self.whatStr() + '\n' + self.whenStr()
        if self.cacheInfo is not None:
            outStr += '\t - ' + str(self.cacheInfo)
        return outStr

    # NOTE: make sure the methods to convert between objects and dictionaries
    #  stay up to date with the members of the class
    def getDict(self):
        """Converts object into dictionary so it can be JSON serialized."""
        return {
            "timestamp" : self.injectionTime,
            "number"    : self.number,
            "section"   : self.section,
            "oldValue"  : self.oldValue,
            "newValue"  : self.newValue,
            "address"   : self.address,
            "sleepTime" : self.sleepTime,
            "cycles"    : self.cycles,
            "PC"        : self.pcVal,
            "name"      : self.name,
            "result"    : self.result.getDict(),
            "cacheInfo" : self.cacheInfo.getDict() if self.cacheInfo is not None else None,
        }

    @classmethod
    def FromDict(cls, d):
        """Factory method to create class from dictionary."""
        il = InjectionLog(
            d["timestamp"],
            d["number"],
            d["section"],
            d["address"],
            d["oldValue"],
            d["newValue"],
            d["name"]
        )
        il.addInjectionInfo(
            d["sleepTime"],
            d["cycles"],
            d["PC"]
        )
        runInfo = d["result"]
        if "core" in runInfo:
            il.addRunLog(RunResult.FromDict(runInfo))
        elif "line" in runInfo:
            il.addRunLog(AssertionFailResult.FromDict(runInfo))
        elif "invalid" in runInfo:
            il.addRunLog(InvalidResult.FromDict(runInfo))
        elif "timeout" in runInfo:
            il.addRunLog(TimeoutResult.FromDict(runInfo))
        elif "message" in runInfo:
            il.addRunLog(AbortResult.FromDict(runInfo))
        elif "task" in runInfo:
            il.addRunLog(StackOverflowResult.FromDict(runInfo))
        else:
            il.result = "Could not deserialize result!"
        if d['cacheInfo'] is not None:
            il.cacheInfo = CacheInfo.FromDict(d['cacheInfo'])
        return il


class gdbState(Enum):
    injectFault = 1
    reset = 2
    finished = 3
    getOutput = 4
    dead = 5
    timeout = 6
