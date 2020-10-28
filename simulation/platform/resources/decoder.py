import re
import sys
import datetime

import resources.utils as utils
from resources.supportClasses import (  RunResult,
                                        InvalidResult,
                                        TimeoutResult,
                                        AssertionFailResult,
                                        AbortResult,
                                        StackOverflowResult,
                                        ErrorMessage,
                                        InfoMessage)

if __name__ == '__main__':
    try:
        import resources.interface as interface
    except ImportError as err:
        print("Error: ", err)
        sys.exit()


class GDBDecoder(object):
    """Decodes incoming lines from the GDB client."""
    def __init__(self):
        self.gdbSkipRegx = re.compile(r'(\(gdb\) )|(\d+\t)')
        self.bpCreateRegx = re.compile(r'Breakpoint [1-9]+ at')
        # g2 = pcVal, g3 = function_name, g4 = arguments,
        #  g6 = source_line
        self.breakAt = re.compile(r'((.*) in )?(\w+|\?\?) \((.*)\)( at (.*))?')
        self.skipList = [
            "The program no longer exists.",
            "Cannot execute this command while the target is running.",
            # This error is sometimes thrown when exiting GDB, but the
            #  'quit' command still works just fine
            "Use the \"interrupt\" command to stop the target",
            "and then try again.",
            "Program received signal SIGINT, Interrupt."
        ]

    def parseline(self, line):
        returnVal = None
        if line is "":
            pass
        elif len(self.gdbSkipRegx.findall(line)) > 0:
            pass
        elif line.startswith("Reading symbols from"):
            pass
        elif line.startswith("Program terminated with"):
            pass
        elif line in self.skipList:
            pass
        elif len(self.bpCreateRegx.findall(line)) > 0:
            pass
        elif len(self.breakAt.findall(line)) > 0:
            pass
        else:
            returnVal = line
        return returnVal


class EmulatorDecoder:
    """Parses things printed to stdout by guest program."""
    def __init__(self):
        self.dataBuf = []
        self.execMatch = re.compile(r"\s*C:\s*(\d+)\s*E:\s*(\d+)\s*F:\s*(\d+)\s*T:\s*(\d+\.\d+|\d+)\s*(s|ms|us)")
        self.assertMatch = re.compile(r"Assert failed in file (.*), line ([0-9]+)")
        self.abortMatch = re.compile(r"(Data|Prefetch)\sabort\swith\s(.*)")
        self.SOMatch = re.compile(r"HALT: Task (.*) overflowed its stack.")

    def parseline(self, line):
        m_exec = re.match(self.execMatch, line)
        m_assert = re.match(self.assertMatch, line)
        m_abort = re.match(self.abortMatch, line)
        m_so = re.match(self.SOMatch, line)

        if m_exec:
            core = int(m_exec.group(1))
            runtime_float = float(m_exec.group(4))
            runtime_unit = m_exec.group(5)
            runTime = utils.changeTimeBase(runtime_float, runtime_unit)

            errors = int(m_exec.group(2))
            faults = int(m_exec.group(3))
            thisRun = RunResult(core, errors, faults, runTime)
            return thisRun

        elif m_assert:
            a_file = m_assert.group(1)
            a_line = m_assert.group(2)
            afr = AssertionFailResult(a_file, a_line)
            return afr

        elif m_abort:
            a_type = m_abort.group(1)
            a_msg = m_abort.group(2)
            ar = AbortResult(a_type, a_msg)
            return ar

        elif m_so:
            task_name = m_so.group(1)
            sor = StackOverflowResult(task_name)
            return sor

        elif len(line) == 0:
            return None

        elif line.startswith("ERROR: "):
            return ErrorMessage(line)

        elif line.startswith("INFO: "):
            return InfoMessage(line)

        else:
            # invalid output
            return InvalidResult(line.strip())


def driver():
    # TODO: out of date
    print("Not to be run as main!", file=sys.stderr)
    return


if __name__ == '__main__':
    driver()
