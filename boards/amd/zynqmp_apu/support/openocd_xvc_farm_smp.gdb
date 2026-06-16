# SMP Zephyr image on Path F — GDB init (CPU0-only OpenOCD, same as non-SMP D2 layout).
#
# Farm XVC: do NOT use openocd_zynqmp_a53_all_nogroup.cfg — arp_examine on CPUs 1-3
# causes JTAG-DP STICKY ERROR; listing 4 targets breaks z_smp_init.
#
# west build -b zynqmp_apu/zynqmp_apu/smp samples/synchronization -d $BUILD_DIR \
#   -- -DEXTRA_CONF_FILE=$ZEPHYR_BASE/boards/amd/zynqmp_apu/support/thread_debug_smp_farm.conf
#
# D2: unset OPENOCD_CFG OR export OPENOCD_CFG=.../openocd_xvc_farm_smp.cfg (CPU0-only)
# D3: aarch64-zephyr-elf-gdb build/zephyr/zephyr.elf -x .../openocd_xvc_farm_smp.gdb
#
# Full checklist: .claude/README-zynqmp-apu-farm-xvc-smp.md
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

echo \n[1/3] SMP image + CPU0-only OpenOCD (expect a53.0 + axi only).\n
echo       Do NOT arp_examine CPUs 1-3 on farm XVC (JTAG sticky error).\n
echo       Build with thread_debug_smp_farm.conf (threads pinned to CPU0).\n

info registers pc
x/4i $pc

delete breakpoints

echo \n[2/3] hb main (CONFIG_MP_MAX_NUM_CPUS=1 in thread_debug_smp_farm.conf).\n
hb main
info breakpoints
continue

echo \n[3/3] At main — hb thread entry points (max 4 HW slots total):\n
echo       hb thread_a_entry_point\n
echo       hb thread_b_entry_point\n
echo       info breakpoints\n
echo       continue\n
echo \nDo NOT use break. Max 4 hb total. Use delete breakpoints to clear.\n
