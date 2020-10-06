"""Contains functions to handle GDB events."""

# library modules
import gdb
import threading

# project modules
import resources.gdbCommands as gdbCommands
import resources.utils as utils
import resources.strings as strings


# globals
serverHandle = None
timerHandle = None


def setServer(s):
    global serverHandle
    serverHandle = s


def checkTimeout():
    """This function acts as a watchdog for timeouts in the execution.

    This is a run by a cancellable timer thread.
    """

    # we had a timeout
    serverHandle.stopThreads = True

    try:
        print("watchdog timer detected a hang! interrupting program...")
        serverHandle.sendResponseMsg(strings.timeoutMsg)
        # gdb.post_event(gdbCommands.Executor("interrupt"))
    except (gdb.GdbError, gdb.error) as e:
        print(e)
        print("Failed to interrupt with timer")


def cont_handler(event):
    """Handle the "continue" event"""
    global serverHandle, timerHandle
    serverHandle.stopThreads = False
    timerHandle = threading.Timer(interval=serverHandle.timeout, function=checkTimeout)
    timerHandle.start()


# https://github.com/tromey/gdb-gui/blob/master/gui/notify.py
# https://sourceware.org/gdb/current/onlinedocs/gdb/Breakpoints-In-Python.html#Breakpoints-In-Python
def stop_handler(event):
    """Handle the "stop" event"""
    global serverHandle
    # serverHandle.sendResponseMsg(strings.stopMsg)

    if isinstance(event, gdb.BreakpointEvent):
        serverHandle.sendResponseMsg(strings.bpMsg)
        serverHandle.sendResponseMsg(event.breakpoint.location)
    elif isinstance(event, gdb.StopEvent):
        if serverHandle.stopThreads:
            # this is if the timeout watchdog caught it
            # serverHandle.sendResponseMsg(strings.timeoutMsg)
            pass
        else:
            # it's the emulator stopping itself
            serverHandle.sendResponseMsg(strings.stopMsg)
    else:
        serverHandle.sendResponseMsg(event)

    timerHandle.cancel()
    # wait for the timer to catch up
    timerHandle.join()
    return


def simple_stop_handler(event):
    """Simple handler for stop events."""
    global serverHandle
    serverHandle.sendResponseMsg(strings.simpleStopMsg)
    return


def exited_handler(event):
    """Handles when the Emulator dies completely.

    The ExitedEvent only has an attribute "inferior", but probably not very helpful.
    An "exit" is signaled by GDB printing
        "Program terminated with signal SIGABRT, Aborted."
        "The program no longer exists."
    This also catches when we finish normally, so ignore that.
    """

    global serverHandle
    if serverHandle.finished:
        return  # no big deal, normal finish
    serverHandle.stopThreads = True
    # signal the exit condition
    serverHandle.sendResponseMsg(strings.deadMsg)
    # cleanup will happen when the server is told to do so
    return
