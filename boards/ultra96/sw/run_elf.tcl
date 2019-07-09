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

# Begin XSDK commands
connect -url tcp:127.0.0.1:3121

puts "All targets: [targets]"

# Get a list of targets that have the jtag_cable_name =~ $cable_filter
# name == "fpga_name" is used because a single cable can have multiple targets (fpga, arm0, arm1, etc)
# -target properties is used to turn a list with 1 item per target.  Otherwise, even if there is only 
# one target, the return value is a space separates list of values and tcl considers the list length > 1
set target_list [targets -nocase -filter {name =~"*A53*0*" && jtag_cable_name =~ $cable_filter} -target-properties]
puts $target_list

if {[llength $target_list] > 1} {
	puts "Error: Multiple matching targets: $target_list (Use JTAG_BOARD_LOC or JTAG_BOARD_FILTER to filter to only one board)"
	exit 1
} elseif {[llength $target_list] == 0} {
    puts "Error: No targets matching filter: $cable_filter"
    exit 1
}

source $::env(XILINX_SDK)/scripts/sdk/util/zynqmp_utils.tcl

# Configure APU
targets -set -nocase -filter {name =~"APU*" && jtag_cable_name =~ $cable_filter} 
loadhw -hw  $BOARD_HW/system.hdf -mem-ranges [list {0x80000000 0xbfffffff} {0x400000000 0x5ffffffff} {0x1000000000 0x7fffffffff}]
configparams force-mem-access 1
source $BOARD_HW/psu_init.tcl
psu_init
after 1000
psu_ps_pl_isolation_removal
after 1000
psu_ps_pl_reset_config
catch {psu_protection}

# Configure A53 core
if {[info exists env(PROGRAM_MP)]} {
	targets -set -nocase -filter {name =~"*A53*0" && jtag_cable_name =~ $cable_filter}
	rst -processor
	dow $::env(ELF_FILE)_0.elf

	targets -set -nocase -filter {name =~"*A53*1" && jtag_cable_name =~ $cable_filter}
	rst -processor
	dow $::env(ELF_FILE)_1.elf

	targets -set -nocase -filter {name =~"*A53*2" && jtag_cable_name =~ $cable_filter}
	rst -processor
	dow $::env(ELF_FILE)_2.elf

	targets -set -nocase -filter {name =~"*A53*3" && jtag_cable_name =~ $cable_filter}
	rst -processor
	dow $::env(ELF_FILE)_3.elf

} else {
	targets -set -nocase -filter {name =~"*A53*0" && jtag_cable_name =~ $cable_filter}
	rst -processor
	dow $::env(ELF_FILE).elf
}

configparams force-mem-access 0

if {[info exists env(PROGRAM_MP)]} {
	targets -set -nocase -filter {name =~"*A53*0" && jtag_cable_name =~ $cable_filter}
	con
	targets -set -nocase -filter {name =~"*A53*1" && jtag_cable_name =~ $cable_filter}
	con
	targets -set -nocase -filter {name =~"*A53*2" && jtag_cable_name =~ $cable_filter}
	con
	targets -set -nocase -filter {name =~"*A53*3" && jtag_cable_name =~ $cable_filter}
	con
} else {
	con
}