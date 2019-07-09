open_hw

connect_hw_server -url localhost:3121
refresh_hw_server

if {[info exists env(JTAG_BOARD_LOC)]} {
	set board_filter $::env(JTAG_BOARD_LOC)
} elseif {[info exist env(JTAG_BOARD_FILTER)]} {
	set board_filter $::env(JTAG_BOARD_FILTER)
} else {
	set board_filter "*"
}

puts "All targets: [get_hw_targets]"
set targets [get_hw_targets $board_filter]

puts "Filtering targets by: $board_filter"

if {[llength $targets] > 1} {
	puts "Error: Multiple matching targets: $targets (Use JTAG_BOARD_LOC or JTAG_BOARD_FILTER to filter to only one board)"
	exit 1
}

current_hw_target [lindex $targets 0]

open_hw_target

if {[info exists env(JTAG_DEVICE_INDEX)]} {
	current_hw_device [lindex [get_hw_devices] $::env(JTAG_DEVICE_INDEX)]
} else {
	current_hw_device [lindex [get_hw_devices] 0]
}
set_property PROGRAM.FILE "$::env(BITSTREAM)" [current_hw_device]

program_hw_devices [current_hw_device]

if {[info exists env(JTAG_REGISTER_DONE_BIT)]} {
	puts "Done bit:[get_property $::env(JTAG_REGISTER_DONE_BIT) [current_hw_device]]"
}

close_hw_target

disconnect_hw_server 