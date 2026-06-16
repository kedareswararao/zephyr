# GDB init — non-SMP zynqmp_apu, OpenOCD Path F (CPU0 only, port 3333).
# No Python required (Zephyr SDK GDB may not ship Python scripting).
#
# Usage:
#   aarch64-zephyr-elf-gdb $BUILD_DIR/zephyr/zephyr.elf \
#     -x $ZEPHYR_BASE/boards/amd/zynqmp_apu/support/openocd_xvc_gdb.gdb

set pagination off
set remotetimeout 120
set mem inaccessible-by-default off
set breakpoint always-inserted on
set remote hardware-breakpoint-limit 4

target extended-remote localhost:3333
monitor halt
monitor targets

echo \n=== PC check: good = low DDR (0x80... or 0x0000...); bad = 0xfffea000 ===\n
info registers pc
x/4i $pc

delete breakpoints

echo \n=== HW breakpoint at main (OpenOCD uses HW, not RAM patch) ===\n
hb main
info breakpoints
continue

echo \n=== Stopped — add thread breakpoints, then continue ===\n
echo   hb thread_a_entry_point\n
echo   hb thread_b_entry_point\n
echo   hb hello_loop\n
echo   info breakpoints\n
echo   continue\n
