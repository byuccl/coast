import sys
import random
from enum import Enum

import resources.benchmarks as benchmarks

# do we have choices available?
if sys.version_info.minor <= 5:
    # https://stackoverflow.com/a/60677879/12940429
    from itertools import accumulate as _accumulate, repeat as _repeat
    from bisect import bisect as _bisect
    def choices(population, weights=None, *, cum_weights=None, k=1):
        """Return a k sized list of population elements chosen with replacement.
        If the relative weights or cumulative weights are not specified,
        the selections are made with equal probability.
        """
        n = len(population)
        if cum_weights is None:
            if weights is None:
                _int = int
                n += 0.0    # convert to float for a small speed improvement
                return [population[_int(random.random() * n)] for i in _repeat(None, k)]
            cum_weights = list(_accumulate(weights))
        elif weights is not None:
            raise TypeError('Cannot specify both weights and cumulative weights')
        if len(cum_weights) != n:
            raise ValueError('The number of weights does not match the population')
        bisect = _bisect
        total = cum_weights[-1] + 0.0   # convert to float
        hi = n - 1
        return [population[bisect(cum_weights, random.random() * total, 0, hi)]
                for i in _repeat(None, k)]
else:
    from random import choices


class MemorySection(object):
    """MemorySection is a representation of a section in an ELF file."""
    def __init__(self, size, start, name=""):
        self.size = int("0x" + size, base=16)
        self.start = int("0x" + start, base=16)
        self.name = name

    def __str__(self):
        return "{}:\n\tsize: {}\tstart: {}\n".format(
                    self.name, hex(self.size), hex(self.start))

    def getRandomAddress(self):
        """Gets a random address in the section space."""
        start = self.start
        end = start + self.size - 0x01
        randAddr = hex(random.randint(start, end))
        return randAddr


class MemoryMap(object):
    """MemoryMap is a representation of ELF section header information."""
    def __init__(self, init, text, rodata, data, bss, stack, heap):
        self.init = init
        self.text = text
        self.rodata = rodata
        self.data = data
        self.bss = bss
        self.stack = stack
        self.heap = heap
        self.list = {
            # only inject faults into RAM
            'init'  : self.init,
            'text'  : self.text,
            'rodata': self.rodata,
            'data'  : self.data,
            'bss'   : self.bss,
            'stack' : self.stack,
            'heap'  : self.heap,
        }

    def __str__(self):
        # use the list to generate the string
        retStr = ""
        for item in self.list.values():
            retStr += str(item)
        return retStr

    def getRandomSection(self):
        return random.choice(list(self.list.values()))


class CachePolicy(Enum):
    POLICY_ROUND_ROBIN = 0
    POLICY_RANDOM = 1

_policy_nums = set(item.value for item in CachePolicy)


class CacheData:
    """Has information about a cache's characteristics."""
    def __init__(self, name, size, assoc, bSize, policy, wordSize=4):
        self.name = name
        # given information
        self.cacheSize = size
        self.associativity = assoc
        self.blockSize = bSize
        self.policy = policy
        # why does it matter if we know the policy here?
        if policy not in _policy_nums:
            raise TypeError("policy {} not valid".format(policy))
        self.wordSize = wordSize

        # calculate the number of rows
        rowBytes = self.blockSize * self.associativity
        self.rows = size / rowBytes

    def randomWordCacheAddr(self):
        randRow = random.randrange(0, self.rows)
        randBlock = random.randrange(0, self.associativity)
        randWord = random.randrange(0, (self.blockSize // self.wordSize))
        return (randRow, randBlock, randWord)


class MemHierarchy:
    """Contains all of the caches."""
    def __init__(self, board):
        if board == "pynq":
            self.icache = CacheData("icache",
                                **benchmarks.getCacheInfo(board, "icache"))
            self.dcache = CacheData("dcache",
                                **benchmarks.getCacheInfo(board, "dcache"))
            self.l2cache = CacheData("l2cache",
                                **benchmarks.getCacheInfo(board, "l2cache"))
        else:
            print("Invalid board for cache setup!", file=sys.stderr)
            sys.exit(-1)

        # thing for getting random cache equally
        self.cacheWeights = [
            self.icache.cacheSize,
            self.dcache.cacheSize,
            self.l2cache.cacheSize,
        ]
        # population for choices
        self.cacheList = [
            self.icache,
            self.dcache,
            self.l2cache,
        ]

    def randomWordCacheAddr(self, cacheName=None):
        # allow caller to specifiy which cache
        if (cacheName is not None):
            if cacheName == "icache":
                randCache = self.icache
            elif cacheName == "dcache":
                randCache = self.dcache
            elif cacheName == "l2cache":
                randCache = self.l2cache
            else:
                randCache = choices(self.cacheList, weights=self.cacheWeights, k=1)[0]
        else:
            randCache = choices(self.cacheList, weights=self.cacheWeights, k=1)[0]
        # unpack dictionary as keyword args
        return (randCache.name, *randCache.randomWordCacheAddr())
