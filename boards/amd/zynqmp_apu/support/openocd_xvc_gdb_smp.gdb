# GDB init — SMP (alias of openocd_xvc_farm_smp.gdb)

set pagination off
set confirm off
set remotetimeout 120
set mem inaccessible-by-default off
set breakpoint always-inserted on
set remote hardware-breakpoint-limit 4

target extended-remote localhost:3333
monitor halt
monitor targets

info registers pc
x/4i $pc

delete breakpoints
hb main
info breakpoints
continue

echo \nAt main: hb thread_a_entry_point / hb thread_b_entry_point / continue\n
