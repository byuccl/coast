import re
import sys
import shlex
from os import path
from queue import Empty
import subprocess as sp
from datetime import datetime

try:
    import resources.mem as mem
    from resources.benchmarks import getObjDumpPath
except ImportError as err:
    print("Error: ", err)
    sys.exit()


# function to read executable file to get memory map
def readElf(filename, objPath=None):
    """Extract the section header information from an ELF file, format as MemoryMap"""
    if not path.exists(filename):
        print("Error, filename {} does not exist!".format(filename), file=sys.stderr)
        sys.exit()

    # so that we can use this outside of the "benchmarks" stuff
    if objPath is None:
        objdump = getObjDumpPath()
    else:
        objdump = objPath
    command = "{} -h {}".format(objdump, filename)
    headers = sp.check_output(shlex.split(command)).decode("utf-8")
    # size is 3rd element, starting address is 4th

    for line in headers.split('\n'):
        if ".init " in line:
            lline = shlex.split(line)
            init = mem.MemorySection(lline[2], lline[3], ".init")
        elif ".text " in line:
            lline = shlex.split(line)
            text = mem.MemorySection(lline[2], lline[3], ".text")
        elif ".rodata " in line:
            lline = shlex.split(line)
            rodata = mem.MemorySection(lline[2], lline[3], ".rodata")
        elif ".data " in line:
            lline = shlex.split(line)
            data = mem.MemorySection(lline[2], lline[3], ".data")
        elif ".bss " in line:
            lline = shlex.split(line)
            bss = mem.MemorySection(lline[2], lline[3], ".bss")
        elif ".stack " in line:
            lline = shlex.split(line)
            stack = mem.MemorySection(lline[2], lline[3], ".stack")
        elif ".heap" in line:
            lline = shlex.split(line)
            heap = mem.MemorySection(lline[2], lline[3], ".heap")
    mmap = mem.MemoryMap(init, text, rodata, data, bss, stack, heap)
    return mmap

"""Example of memory section output:

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00005d00  00100000  00100000  00010000  2**6
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
 12 .heap         0140000c  00110e84  00110e84  00020010  2**0
                  ALLOC
"""


# borrowed from https://github.com/james-ben/miscellany/blob/master/python/utils/make_nice_comments.py
def centerText(text, surround='-', width=72):
    text = " " + text + " "
    return "{0:{c}^{n}}".format(text, c=surround, n=width)


def getFormattedTime(now=None):
    if now is None:
        now = datetime.now()
    return now.strftime("%Y-%m-%d %H:%M:%S.%f")


def reverseFormatTime(ts):
    return datetime.strptime(ts, "%Y-%m-%d %H:%M:%S.%f")


def changeTimeBase(t, base='us'):
    if base == "ms":
        runtime = t / 1000.0
    elif base == "us":
        runtime = t / 1000000.0
    else:
        runtime = t
    return runtime


# center of range used to calculate +/- 10%
def withinFloatRange(t, center, perc=0.1):
    lw = center * (1-perc)
    up = center * (1+perc)
    return ((t < up) and (t > lw))


# https://stackoverflow.com/a/14693789
ansi_escape = re.compile(r'\x1B\[[0-?]*[ -/]*[@-~]')
def stripANSIcodes(line):
    return ansi_escape.sub('', line)

# https://stackoverflow.com/a/36598450/12940429
hexCodes = re.compile(r'[^\x00-\x7f]')
def stripHexCodes(line):
    return hexCodes.sub('', line)


def emptyQueue(q):
    """Remove all remaining messages from a queue."""
    while True:
        try:
            q.get(timeout=0.01)
        except Empty:
            break
