source ../../../../legup.tcl
set_project Artix7 nexys4ddr hw_only

set_parameter CAD_TOOL "vivado"
set_parameter DIVIDER_MODULE "generic"

set_parameter DEBUG_FILL_DATABASE 1

set_parameter DEBUG_INSERT_DEBUG_RTL 1

set_parameter DEBUG_CORE_TRACE_REGS 1
set_parameter DEBUG_CORE_TRACE_REGS_DELAY_ALL 1

set_parameter DEBUG_CORE_SIZE_BUFS_STATIC_ANALYSIS 1

set_parameter LOCAL_RAMS 1

set_parameter DEBUG_CORE_COMBINE_MEM_REG_BUFS 1
set_parameter DEBUG_CORE_MEM_SIGS_FROM_VALS 0
set_parameter DEBUG_CORE_USE_MEM_CTRL_SIGS_FOR_REGS 0

set_parameter DEBUG_CORE_COMBINE_CTRL_REG_BUFS 1

set_parameter DEBUG_CORE_SUPPORT_LIVE_READ_FROM_MEM 0


#set_parameter DEBUG_CORE_COMBINE_MEM_REG_BUFS 1
#set_parameter DEBUG_CORE_MEM_SIGS_FROM_VALS 1
#set_parameter DEBUG_CORE_USE_MEM_CTRL_SIGS_FOR_REGS 1

set_parameter DEBUG_CORE_TRACE_MEM_READS 0
set_parameter DEBUG_CORE_TRACE_REG_READS 0
