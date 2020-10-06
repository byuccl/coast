# Strings of commands and messages. These are used in many places, so this defines
#  a standard interface for passing messages.
# Essentially, this file is a list of global, read-only constants

import os


# response messages
configGdbMsg = "configuring GDB"
setTimeoutMsg = "correctly set timeout"
handlerSetupMsg = "setting up GDB event handlers"
finishedMsg = "Finished"
quitMsg = "\n...OK, fine"
killGdbMsg = "Killing GDB"
stopMsg = "hit stop handler"
timeoutMsg = "Timeout detected"
bpMsg = "hit breakpoint"
deadMsg = "GDB died!"
queueStopMsg = "DIE!!"
# is this necessary any more?
intervalRestartMsg = "Iteration count requires restart"
simpleStopMsg = "Hit the basic stop handler"
underTimeMsg = "Ran too short"
normalTimeMsg = "Within expected time range"
invalidRange = "invalid address for reading/writing"

# command strings
configGdbCmd = "configureGDB"
silentConfigCmd = "configure GDB quietly"
setTimeoutCmd = "setTimeout"
handlerSetupCmd = "setUpHandlers"
contCmd = "continue"
quitCmd = "quit"
killGdbCmd = "kill GDB"
silentKillCmd = "kill GDB quietly"
killEmulatorCmd = "kill Emulator Server"
reloadCmd = "reload"
finishCmd = "stop listening"
# TODO: remove this UART stuff
discardUartResultCmd = "Slept too long to allow injection"
interruptCmd = "interrupt GDB"
getVarCmd = "get variable value"
readGlblTimerCmd = "read global timer"
readRegCmd = "read register value"
writeRegCmd = "write register value"
readMemCmd = "read memory address"
writeMemCmd = "write memory address"
memNameCmd = "get symbol name (address)"
regNameCmd = "get symbol name (register)"
symbolAddrCmd = "get symbol address"
resetSocketCmd = "reset socket handle"
disconnectCmd = "disconnect from server"
reconnectCmd = "reconnect to server"
injectingDoneMsg = "done changing memory"
gdbExecCmd = "execute command"

# injections
killEmulator = "set PC = 0x00"
createSigtrap = "set S4 = 0x800005a5"
breakPriv = "set PRIV = 0x00040003"
breakPrinting = "set S6 = 0x20403570"
tProbeName = "__tProbe"

# file paths
gdbScriptName = "gdbClient.py"

# queue sources
qSrcGDB = "fromGDB"
qSrcEmulator = "fromEmulator"
qSrcSocket = "fromSocket"
qSrcSupervisor = "fromSupervisor"
qSrcThreads = "fromThreads"
qSrcQueues = "fromQueues"
