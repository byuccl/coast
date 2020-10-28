import re
import shlex
import subprocess as sp

from resources.utils import readElf


# globals
objdump_name = "objdump"


# example output
"""
./build/rtos_test.xMR/rtos_test.xMR.elf:     file format elf32-littlearm

SYMBOL TABLE:
00100000 l    d  .text  00000000 .text
00000000 l    df *ABS*  00000000 build/rtos_test.xMR/port_asm_vectors.o
00100034 l       .text  00000000 FIQLoop
00000000 l    df *ABS*  00000000 build/rtos_test.xMR/boot.o
f8f02000 l       *ABS*  00000000 PSS_L2CC_BASE_ADDR
001000f8 l       .text  00000000 OKToRun
00102c70 l     F .text  00000010 __timerMsgCallbackOnce
00134814 l     O .bss   00000004 _sbrk.heap
00124194 l     O .bss   00000020 allEventParams
0012426c l     O .bss   00000004 goalIdx_TMR
0011df6c l     O .data  00000004 goalVal
00106c9c l     F .text  00002070 prvTimerTask
00000000 l    df *ABS*  00000000 xil-crt0.S
0011dd8c l     O .rodata        00000010 zeroes.6926
00000000         *UND*  00000000 SIM_MODE
00120000 g       .mmu_tbl       00000000 __mmu_tbl_start
01534a30 g       .heap  00000000 _heap_end
00104860  w    F .text  0000000c vApplicationMallocFailedHook
02934a30 g       .stack 00000000 _irq_stack_end
"""
# "l"  "g"  "u"  "!" 
# The symbol is a local (l), global (g), unique global (u), neither global nor
#  local (a space) or both global and local (!).
# "w" The symbol is weak (w) or strong (a space).
# "C" The symbol denotes a constructor (C) or an ordinary symbol (a space).
# "W" The symbol is a warning (W) or a normal symbol (a space). 
# "I"  "i" The symbol is an indirect reference to another symbol (I), a function
#  to be evaluated during reloc processing (i) or a normal symbol (a space).
# "d"  "D" The symbol is a debugging symbol (d) or a dynamic symbol (D) or
#  a normal symbol (a space).
# "F"  "f"  "O" The symbol is the name of a function (F) or a file (f) or
#  an object (O) or just a normal symbol (a space).

# *ABS* is absolute section
# *UND* is section is referenced but not defined in the file being dumped
# next column is alignment for common symbols and size for other symbols


class Symbol:
    def __init__(self, addr, name, section, flags):
        self.address = int(addr, 16)
        self.name = name
        self.section = section
        self.flags = flags

# TODO: function is just a symbol?
class Function(Symbol):
    def __init__(self, addr, name, section, flags, size):
        super().__init__(addr, name, section, flags)
        self.size = int(size, 16)
        self.end = self.address + self.size

class Object(Symbol):
    def __init__(self, addr, name, section, flags, size):
        super().__init__(addr, name, section, flags)
        self.size = int(size, 16)
        self.end = self.address + self.size

class Common(Symbol):
    def __init__(self, addr, name, section, flags, alignment):
        super().__init__(addr, name, section, flags)
        self.alignment = int(alignment, 16)


def decodeLine(line):
    addr, line2 = line.split(maxsplit=1)
    # next 7 characters as above
    symFlags = list(line2[:7])
    # get rid of tabs
    line3 = line2[8:].replace('\t', ' ').split(maxsplit=2)
    # there is sometimes a ".hidden" tag before the name

    # section
    section, alignment, name = line3

    # TODO add more classes
    if symFlags[6] == 'F':
        return Function(addr, name, section, symFlags, alignment)
    elif symFlags[6] == ' ':
        return Common(addr, name, section, symFlags, alignment)
    else:
        return Object(addr, name, section, symFlags, alignment)


class ElfParser:
    def __init__(self):
        self.functionMap = None
        self.symbolMap = None

    def createSymTable(self, binPath, objPath=objdump_name):
        # get symbol table from objdump
        cmd = "{obj} -t {bin}".format(
            obj=objPath,
            bin=binPath
        )
        proc = sp.Popen(shlex.split(cmd), stdout=sp.PIPE, stderr=sp.PIPE)
        output, _ = proc.communicate()
        lines = output.decode().splitlines()
        # skip the header and footer
        symLines = sorted(lines[4:-2])
        syms = {}
        functions = {}

        # decode each line
        for line in symLines:
            if not line:
                continue
            symbol = decodeLine(line)
            if isinstance(symbol, Function):
                functions[symbol.address] = symbol
            else:
                syms[symbol.address] = symbol
        
        self.functionMap = functions
        self.symbolMap = syms

        # now look at the section headers
        self.memMap = readElf(binPath, objPath=objPath)

    def findNearestSymbolName(self, addr):
        # first look through functions
        for f in self.functionMap.values():
            if (addr >= f.address) and (addr < f.end):
                return "{}+0x{:04X}".format(f.name, f.end-addr)
        # then through the symbols
        for s in self.symbolMap.values():
            if isinstance(s, Object):
                if (addr >= s.address) and (addr < s.end):
                    return "{}+0x{:04X}".format(s.name, s.end-addr)
            else:
                if addr == s.address:
                    return s.name
        # now just look in the sections
        for name, section in self.memMap.list.items():
            end = section.start + section.size
            if (addr >= section.start) and (addr < end):
                return "section:{}+0x{:04X}".format(name, end-addr)
        # couldn't find it
        return None

    def findNearestSymbol(self, addr):
        # first look through functions
        for f in self.functionMap.values():
            if (addr >= f.address) and (addr < f.end):
                return f
        # then through the symbols
        for s in self.symbolMap.values():
            if isinstance(s, Object):
                if (addr >= s.address) and (addr < s.end):
                    return s
            else:
                if addr == s.address:
                    return s.name
        # now just look in the sections
        for name, section in self.memMap.list.items():
            end = section.start + section.size
            if (addr >= section.start) and (addr < end):
                return "section:{} + 0x{:04X}".format(name, end-addr)
        # couldn't find it
        return None
