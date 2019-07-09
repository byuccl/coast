importPackage(Packages.com.ti.debug.engine.scripting)
importPackage(Packages.com.ti.ccstudio.scripting.environment)
importPackage(Packages.java.lang)
var timeoutLength = 10;
var board_name = "Texas Instruments XDS110 USB Debug Probe/CortexR5"
var exec_path = arguments[0];
var config_path = arguments[1];

var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
script.setScriptTimeout(timeoutLength * 1000);
ds.setConfig(config_path);

var debugSession = ds.openSession(board_name);
debugSession.options.setString("FlashEraseSelection", "Necessary Sectors Only (for Program Load)");

debugSession.target.connect();

script.traceSetConsoleLevel(TraceLevel.OFF);
print("Loading program " + exec_path)
debugSession.memory.loadProgram(exec_path);
script.traceSetConsoleLevel(TraceLevel.INFO);

debugSession.target.runAsynch();

// debugSession.target.halt();
debugSession.target.disconnect();
debugSession.terminate();
ds.stop();
print("Finished\r\n")


//end
