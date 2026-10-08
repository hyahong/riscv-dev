# Adapted from fpga0:/home2/yahong/workspace/program.tcl.
if {[llength $argv] != 1} {error "Specify exactly one validated bitstream"}
set bitfile [file normalize [lindex $argv 0]]
if {![file isfile $bitfile]} {error "Bitstream not found: $bitfile"}
open_hw_manager
connect_hw_server -url localhost:3121
set targets [get_hw_targets]
if {[llength $targets] != 1} {error "Expected one JTAG target; found: $targets"}
puts "SELECTED_JTAG_TARGET=$targets"
current_hw_target [lindex $targets 0]
open_hw_target
set devices [get_hw_devices -quiet xcvu9p_*]
if {[llength $devices] != 1} {error "Expected one XCVU9P; found: [get_hw_devices]"}
set device [lindex $devices 0]
current_hw_device $device
set_property PROGRAM.FILE $bitfile $device
set_property PROBES.FILE {} $device
puts "SELECTED_PROGRAM_FILE=[get_property PROGRAM.FILE $device]"
program_hw_devices $device
refresh_hw_device -update_hw_probes false $device
set done_count 0
foreach property [list_property $device] {
    if {[string match "REGISTER.CONFIG_STATUS.*DONE_PIN" $property]} {
        set value [get_property $property $device]
        puts "$property=$value"
        if {$value ne "1"} {error "FPGA DONE not asserted: $property=$value"}
        incr done_count
    }
}
if {$done_count != 3} {error "Expected DONE for all three SLRs, found: $done_count"}
puts "FPGA_PROGRAM_ALL_SLR_DONE_OK"
close_hw_manager
exit 0
