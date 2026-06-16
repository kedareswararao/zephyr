# Breakpoint / memory diagnostic — run when hb/break fails (no Python).
#
# Usage:
#   aarch64-zephyr-elf-gdb $BUILD_DIR/zephyr/zephyr.elf \
#     -x $ZEPHYR_BASE/boards/amd/zynqmp_apu/support/openocd_xvc_diag.gdb
#
# Paste the full GDB output + matching OpenOCD D2 log into the bug report.

set pagination off
set remotetimeout 120
set mem inaccessible-by-default off

target extended-remote localhost:3333
monitor halt
monitor targets

echo \n========== 1. TARGET STATE ==========\n
info registers pc sp
x/4i $pc
bt

echo \n========== 2. GDB SYMBOL ADDRESSES ==========\n
info address main
info address thread_a_entry_point
info address thread_b_entry_point
info address hello_loop
info address bg_thread_main

echo \n========== 3. GDB MEMORY READ (no breakpoint) ==========\n
x/4i main
x/4i hello_loop

echo \n========== 4. OPENOCD MEMORY READ (monitor mdw) ==========\n
monitor reg pc
eval "monitor mdw 0x%lx 4", main
eval "monitor mdw 0x%lx 4", hello_loop

echo \n========== 5. HW BREAKPOINT TRIAL ==========\n
delete breakpoints
set remote hardware-breakpoint-limit 4
hb main
info breakpoints
continue

echo \n========== 6. IF main HW BP worked, try thread BPs ==========\n
hb thread_a_entry_point
hb thread_b_entry_point
hb hello_loop
info breakpoints

echo \n=== DONE ===\n
echo If HW breakpoints hit main/thread_a/thread_b: non-SMP Path F debug WORKS.\n
echo Harmless noise: pc 0x0 / z_mapped_start in thread arg display (GDB symtab, not HW BP fail).\n
echo Clear breakpoints: delete breakpoints  (not remove breakpoints)\n
echo OpenOCD keep_alive warnings: increase set remotetimeout (this script uses 120).\n
echo Attach PC 0xfffea000 is OK if hb main fires on continue; flash-halt gives cleaner PC.\n
