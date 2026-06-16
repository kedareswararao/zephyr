# 4-CPU SMP RCA — GDB diag (does NOT auto-continue).
#
# Requires thread_debug_smp_farm_4cpu_diag.conf build.
# D2: unset OPENOCD_CFG (CPU0-only OpenOCD).
#
# See: .claude/README-zynqmp-apu-farm-xvc-smp-rca.md

set pagination off
set confirm off
set remotetimeout 120
set mem inaccessible-by-default off
set breakpoint always-inserted on
set remote hardware-breakpoint-limit 4

target extended-remote localhost:3333
monitor halt
monitor targets

echo \n=== 4-CPU SMP RCA diag ===\n
echo Watch SERIAL during continue: Failed to boot secondary / Secondary CPU is up\n
echo Targets (expect CPU0-only OpenOCD): a53.0 + axi only\n
echo DO NOT arp_examine CPUs 1-3 on farm XVC.\n

info registers pc
x/4i $pc

delete breakpoints
hb main
info breakpoints

echo \n=== Run: continue ===\n
echo If hang: Ctrl-C, then: info registers pc / bt\n
echo If stop at main: 4-CPU SMP bring-up succeeded under debugger.\n
echo RCA doc: .claude/README-zynqmp-apu-farm-xvc-smp-rca.md\n
