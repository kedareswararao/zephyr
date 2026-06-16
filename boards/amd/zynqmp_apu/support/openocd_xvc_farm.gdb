# Non-SMP Path F — GDB init (stops at main; add thread hb manually).
# D2: openocd_xvc_remote.sh server  (default openocd_xvc_farm.cfg)
# D3: aarch64-zephyr-elf-gdb build/zephyr/zephyr.elf -x .../openocd_xvc_farm.gdb
#
# Full checklist: .claude/README-zynqmp-apu-farm-xvc-non-smp.md
# Use hb ONLY — never break.

set pagination off
set confirm off
set remotetimeout 120
set mem inaccessible-by-default off
set breakpoint always-inserted on
set remote hardware-breakpoint-limit 4

target extended-remote localhost:3333
monitor halt
monitor targets

echo \n[1/3] Targets: expect uscale.a53.0 + uscale.axi only (non-SMP).\n
echo       If a53.1-.3 appear, D2 is using farm_SMP.cfg — restart D2 with default cfg.\n

info registers pc
x/4i $pc

delete breakpoints

echo \n[2/3] Setting HW breakpoint at main (hb only)...\n
hb main
info breakpoints

echo \n[3/3] continue — should stop at main. Then run:\n
echo       hb thread_a_entry_point\n
echo       hb thread_b_entry_point\n
echo       info breakpoints\n
echo       continue\n
echo \nDo NOT use break. Max 4 hb total. Use delete breakpoints to clear.\n

continue
