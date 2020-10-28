# based on code found at
# https://stackoverflow.com/a/60386350/12940429

# import the c library with usleep in it
from ctypes import CDLL
libc = CDLL('libc.so.6')


class Sleeper:
    """Wrapper around clib usleep."""
    def __init__(self):
        self.sleepFn = libc.usleep

    def sleep(self, seconds):
        """Sleep is given in seconds, converted to microseconds."""
        sleepTime = int(seconds*1000000)
        self.sleepFn(sleepTime)


################################# Unit testing #################################

if __name__ == "__main__":
    from time import perf_counter
    from numpy import mean, std


def testTime(sleeper, t, iterations=50):
    """Try sleeping for time t for a given number of iterations."""
    timeList = []
    for _ in range(iterations):
        # perf_counter gives most accurate* timing results
        begin_ts = perf_counter()
        sleeper.sleep(t)
        end_ts = perf_counter()
        timeList.append(end_ts - begin_ts)
    return timeList


def testMain():
    s = Sleeper()
    # times to test sleeping for
    testVectors = [
        # 5, 2, 1,
        0.5, 0.1,
        0.05, 0.01,
        0.005, 0.001,
        0.00005, 0.0001
    ]

    for t in testVectors:
        # get list of time diffs
        actualTimes = testTime(s, t)
        # find mean and standard deviation
        m = mean(actualTimes)
        stdev = std(actualTimes)
        # find the percent error
        percError = (abs(m - t) / t) * 100.0
        # print result
        print("sleep {:6}, mean: {:1.6f}, stdev: {:1.6f}, error: {:3.6f}%".format(t, m, stdev, percError))


if __name__ == "__main__":
    testMain()
