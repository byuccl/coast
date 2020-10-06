# data that is unique to each of the different benchmarks
# keep this up to date before testing any new files

import os

import resources.registers as registers

# global board and benchmark
runBoard = None
runBench = None


class Benchmark(object):
    """docstring for Benchmark."""
    def __init__(self, prefix, bp, st=None):
        self.prefix = prefix
        if not isinstance(bp, list):
            self.bpLocation = [bp]
        else:
            self.bpLocation = bp
        # gained by experimenting
        self.maxSleepTime = st


# matches the name of the source directory with the filename and line number
#  where the breakpoint should be placed
riscvBenchmarks = [
    Benchmark("matrixMultiply", "mm.c:30", st=0.3),
    Benchmark("matrixMultiply.tmr", "mm_tmr.c:26", st=0.5),
    Benchmark("sha256", "sha256.c:29", st=0.3),
    Benchmark("sha256.tmr", "sha256_tmr.c:30", st=0.4),
]

pynqBenchmarks = [
    # original benchmark
    Benchmark("matrixMultiply", "mm.c:42", st=0.25),
    Benchmark("matrixMultiply.tmr", "mm_tmr.c:44", st=0.3),
    # rtos benchmarks
    Benchmark("rtos_kUser", ["main.c:83", "main.c:108"]),
    Benchmark("rtos_kUser.xMR", ["main.c:84", "main.c:116"]),
    Benchmark("rtos_kUser.app.xMR", ["main.c:84", "main.c:116"]),
    Benchmark("rtos_kUser.kernel.xMR", ["main.c:105", "main.c:137"]),
    Benchmark("rtos_kUser.stack.xMR", ["main.c:84", "main.c:116"]), # same source files
    Benchmark("rtos_test", ["main.c:71", "main.c:87"]),
    Benchmark("rtos_test.xMR", ["main.c:82", "main.c:97"]),
    Benchmark("rtos_test.app.xMR", ["main.c:82", "main.c:97"]),
    Benchmark("rtos_mm", ["main.c:49", "Print.c:76"]),
    Benchmark("rtos_mm.xMR", ["main.c:53", "Print.c:78"]),
    Benchmark("rtos_mm.app.xMR", ["main.c:53", "Print.c:78"]),
    Benchmark("rtos_fat_demo", ["main.c:224", "Print.c:84"]),
    Benchmark("rtos_fat_demo.xMR", ["main.c:229", "Print.c:86"]),
    Benchmark("rtos_fat_demo.app.xMR", ["main.c:229", "Print.c:86"]),
    # new radtest2019 benchmarks
    Benchmark("crc32.build", "crc_32.c:276"),
    Benchmark("crc32.tmr.build", "crc_32.tmr.c:248"),
    Benchmark("dijkstra.build", "dijkstra.c:313"),
    Benchmark("dijkstra.tmr.build", "dijkstra.tmr.c:309"),
    Benchmark("fft.build", "main.c:204"),
    Benchmark("fft.tmr.build", "main.c:217"),
    # TODO: L2 versions
    Benchmark("mm.build", "mm.c:62"),
    Benchmark("mm_tmr.build", "mm_tmr.c:75"),
    Benchmark("nanojpeg.build", "nanojpeg.c:243"),
    Benchmark("nanojpeg.tmr.build", "nanojpeg.tmr.c:281"),
    Benchmark("qsortLib.build", "qsortLib.c:130"),
    Benchmark("qsortLib.tmr.build", "qsortLib.tmr.c:139"),
    Benchmark("qsortNum.build", "qsortNum.c:168"),
    Benchmark("qsortNum.tmr.build", "qsortNum.tmr.c:173"),
    Benchmark("sha256.build", "sha256.c:60"),
    Benchmark("sha256.tmr.build", "sha256_tmr.c:71"),
    Benchmark("susan.build", "susan.c:2337"),
    Benchmark("susan.tmr.build", "susan.tmr.c:2330", st=2.0),
]


# return True on success
def setBoardBenchmark(board, bench):
    global runBoard, runBench
    for b in boards:
        if b.name == board:
            runBoard = b
    for b in runBoard.benchmarks:
        if b.prefix == bench:
            runBench = b
    if (runBoard is not None) and (runBench is not None):
        return True
    else:
        return False


def getBpLocation():
    if runBench is None:
        return None
    else:
        return runBench.bpLocation

def getUpperTimeBound():
    if runBench is None:
        return None
    else:
        return runBench.maxSleepTime


# TODO: move board information to it's own file
class Board(object):
    def __init__(self, n, sc, m, c, r, d, gn, odn, b):
        self.name = n
        self.script = sc            # emulator executable
        self.machine = m            # emulator machine
        self.cpu = c                # emulator cpu
        self.reg = r                # set of registers
        self.binDir = d             # architecture specific binaries
        self.gdbName = gn           # name of the gdb binary
        self.objDumpName = odn      # name of the objdump binary
        self.benchmarks = b         # list of benchmarks


boards = [
    Board(
        "hifive1",
        "qemu-system-riscv32",
        "unknown",
        "unknown",
        registers.RiscvRegister,
        os.path.join(os.environ['HOME'],
            "fault-injection/binutils-gdb/install-riscv/bin/"),
        "riscv32-unknown-elf--gdb",
        "riscv32-unknown-elf--objdump",
        riscvBenchmarks,
    ),
    Board(
        "pynq",
        # "qemu-system-arm",
        os.path.abspath(os.path.join(os.path.abspath(__file__),
                    "../../../qemu-ccl/install/bin/qemu-system-arm")),
        "xilinx-zynq-a9",
        "cortex-a9",
        registers.A9Register,
        os.path.join(os.environ['HOME'],
            "fault-injection/binutils-gdb/install-arm/bin/"),
        "arm-none-eabi-gdb",
        "arm-none-eabi-objdump",
        pynqBenchmarks,
    ),
]


def getScript():
    if runBoard is None:
        return None
    else:
        return runBoard.script

def getMachine():
    if runBoard is None:
        return None
    else:
        return runBoard.machine

def getCpu():
    if runBoard is None:
        return None
    else:
        return runBoard.cpu

def getReg():
    if runBoard is None:
        return None
    else:
        return runBoard.reg

def getGdbPath():
    if runBoard is None:
        return None
    else:
        return os.path.join(runBoard.binDir, runBoard.gdbName)

def getObjDumpPath():
    if runBoard is None:
        return None
    else:
        return os.path.join(runBoard.binDir, runBoard.objDumpName)


# information about caches
_cacheInfo = {
    "pynq" : {
        "icache" : {
            "size" : 32768,
            "assoc" : 4,
            "bSize" : 32,
            "policy" : 0,
        },
        "dcache" : {
            "size" : 32768,
            "assoc" : 4,
            "bSize" : 32,
            "policy" : 0,
        },
        "l2cache" : {
            "size" : 524288,
            "assoc" : 8,
            "bSize" : 32,
            "policy" : 1,
        },
    }
}

def getCacheInfo(board, name):
    if board in _cacheInfo:
        bd = _cacheInfo[board]
        if name in bd:
            return bd[name]
    return None
