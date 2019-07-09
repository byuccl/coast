set BOARD_SW $::env(BOARD_DIR)/sw
set BOARD_HW $::env(BOARD_DIR)/hw

if {[info exists env(JTAG_CABLE_SERIAL)]} {
	puts "JTAG_CABLE_SERIAL:$::env(JTAG_CABLE_SERIAL)"
} 
if {[info exist env(JTAG_CABLE_FILTER)]} {
	puts "JTAG_CABLE_FILTER:$::env(JTAG_CABLE_FILTER)"
}

if {[info exists env(JTAG_CABLE_SERIAL)]} {
	set cable_filter $::env(JTAG_CABLE_SERIAL)
} elseif {[info exist env(JTAG_CABLE_FILTER)]} {
	set cable_filter $::env(JTAG_CABLE_FILTER)
} else {
	set cable_filter "*"
}
puts "Cable filter: $cable_filter"

connect -url tcp:127.0.0.1:3121

puts "Cable targets:\n [targets -filter {jtag_cable_name =~ $cable_filter} ]"

# Get a list of targets that have the jtag_cable_name =~ $cable_filter
# name == "fpga_name" is used because a single cable can have multiple targets (fpga, arm0, arm1, etc)
# -target properties is used to turn a list with 1 item per target.  Otherwise, even if there is only 
# one target, the return value is a space separates list of values and tcl considers the list length > 1
set target_list [targets -nocase -filter {name =~ "xc7z020*" && jtag_cable_name =~ $cable_filter} -target-properties]

if {[llength $target_list] > 1} {
	puts "Error: Multiple matching targets: $target_list (Use JTAG_BOARD_LOC or JTAG_BOARD_FILTER to filter to only one board)"
	exit 1
} elseif {[llength $target_list] == 0} {
    puts "Error: No targets matching filter: $cable_filter"
    exit 1
}

source $BOARD_HW/ps7_init.tcl

# Init the APU
targets -set -nocase -filter {name =="APU" && jtag_cable_name =~ $cable_filter} -index 0
loadhw -hw $BOARD_HW/system.hdf -mem-ranges [list {0x40000000 0xbfffffff}]
configparams force-mem-access 1
stop
ps7_init
ps7_post_config

# Init the individual ARM core
if {[info exists env(PROGRAM_MP)]} {
	targets -set -nocase -filter {name =~ "ARM*#0" && jtag_cable_name =~ $cable_filter} -index 0
	rst -processor

	targets -set -nocase -filter {name =~ "ARM*#1" && jtag_cable_name =~ $cable_filter} -index 0
	rst -processor

	targets -set -nocase -filter {name =~ "ARM*#0" && jtag_cable_name =~ $cable_filter} -index 0
	dow $::env(ELF_FILE)_0.elf

	targets -set -nocase -filter {name =~ "ARM*#1" && jtag_cable_name =~ $cable_filter} -index 0
	dow $::env(ELF_FILE)_1.elf

} else {
	targets -set -nocase -filter {name =~ "ARM*#0" && jtag_cable_name =~ $cable_filter} -index 0
	rst -processor
	dow $::env(ELF_FILE).elf
}

configparams force-mem-access 0

if {[info exists env(PROGRAM_MP)]} {
	targets -set -nocase -filter {name =~ "ARM*#0" && jtag_cable_name =~ $cable_filter} -index 0
	con
	targets -set -nocase -filter {name =~ "ARM*#1" && jtag_cable_name =~ $cable_filter} -index 0
	con
} else {
	con
}
