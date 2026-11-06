<!--
SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
SPDX-License-Identifier: Apache-2.0
-->

# ARM Cortex-A/R latency micro-benchmark

Measures, in CPU cycles via the PMU cycle counter (the same counter the FreeRTOS
perf numbers use), two quantities, apples-to-apples with FreeRTOS:

- **Task context-switch time**: two equal-priority threads pinned to one CPU
  ping-pong with `k_yield()`. The yielder reads the cycle counter just before
  yielding; the catcher reads it as the first thing on entry. The difference is
  one yield-driven context switch (FreeRTOS `taskYIELD()` analogue).
- **Interrupt latency**: read the cycle counter, raise a GIC SGI to this CPU,
  read the counter as the first instruction of the ISR. The difference is the
  trigger->ISR-entry latency (FreeRTOS SGI analogue).

Both run pinned to CPU 0. Build single-core (non-SMP) to match FreeRTOS exactly;
on an SMP board the pinning keeps the work on one core.

Runs on any ARM Cortex-A (AArch64) or Cortex-R (AArch32) target with a GIC and a
usable PMU cycle counter. QEMU does not emulate the PMU cycle counter, so QEMU is
for compile validation only - meaningful numbers require real hardware with PMU
access enabled by the bootloader/firmware.

## Cycle counter per architecture

- **AArch64 (Cortex-A)**: PMUv3 `PMCCNTR_EL0` via `<zephyr/arch/arm64/pmuv3.h>`
  (`CONFIG_ARM64_PMUV3`, supplied for arm64 by `prj_arm64.conf`).
- **AArch32 Cortex-R (Armv7-R R5, Armv8-R R52)**: the CP15 PMU cycle counter -
  the same counter and enable sequence the FreeRTOS R5 benchmark uses: set
  `PMCR.E`, clear `PMCR.D` (no /64 divider), enable the cycle counter in
  `PMCNTENSET` bit 31, read `PMCCNTR` at CP15 `c9,c13,0`. The CPU MHz is
  calibrated at runtime against the system timer, so it relies on the board's
  timer `clock-frequency` being correct (the raw cycle counts are unaffected).

## Integer vs FP context switch

Zephyr switches FP context lazily: the context switch only flips FP access
permission, and the actual save/restore of the FP registers happens on the first
FP access after the switch (a trap). FreeRTOS saves the full FP file on every
switch. The scenarios make the comparison explicit:

- `benchmark.arm_latency.arm64` (default, `CONFIG_FPU_SHARING=y`): both ping-pong
  threads touch FP, so the trap (save previous owner + restore this thread) falls
  inside the measured window. This mimics FreeRTOS - use this number against the
  FreeRTOS one.
- `benchmark.arm_latency.arm64.int` (`CONFIG_FPU=n`): integer-only switch,
  Zephyr's native cost when threads do not use FP (lower by design, lazy FP).
- `benchmark.arm_latency.cortex_r` (`CONFIG_FPU=n`): Cortex-R integer switch,
  Zephyr's native lazy-FP cost when threads do not use FP.

The Xilinx FreeRTOS ports (A-profile and Cortex-R alike) build with
`configUSE_TASK_FPU_SUPPORT=2`, which saves the full FP file on every switch, so
the FP-on number is the FreeRTOS-comparable one on both profiles. `prj.conf`
therefore defaults `CONFIG_FPU=y`, and a plain `west build` reports the
comparable number. Note Zephyr saves FP lazily (a trap on first FP use), so its
FP-on switch is dearer than FreeRTOS's inline save; the integer scenario shows
the cheaper lazy path that wins when FP is unused across a switch.

The banner prints `fp_switch=0/1` so logs are self-describing. Interrupt latency
is identical in both scenarios.

## Output

```
*** ARM Cortex-A/R latency micro-benchmark ***
latency benchmark: CPU 0, measured CPU freq <N> MHz, fp_switch=<0/1>
CONTEXT_SWITCH: iters=<n> min=<c> avg=<c> max=<c> cycles
IRQ_LATENCY: iters=<n> min=<c> avg=<c> max=<c> cycles
latency benchmark: done
```

Convert to microseconds with the printed measured CPU freq (not the dts
clock-frequency): `usecs = cycles / freq_mhz`.

## Build / run

```
# AArch64 Cortex-A, FreeRTOS-equivalent (FP saved on every switch):
west build -b qemu_cortex_a53 tests/benchmarks/arm_latency \
	-- -DEXTRA_CONF_FILE=prj_arm64.conf

# AArch64 integer-only switch:
west build -b qemu_cortex_a53 tests/benchmarks/arm_latency \
	-- -DEXTRA_CONF_FILE=prj_arm64.conf -DCONFIG_FPU=n

# AArch32 Cortex-R, FreeRTOS-equivalent FP switch (the default, no flags needed):
west build -b qemu_cortex_r5 tests/benchmarks/arm_latency

# AArch32 Cortex-R integer-only switch (Zephyr's lazy-FP best case):
west build -b qemu_cortex_r5 tests/benchmarks/arm_latency -- -DCONFIG_FPU=n

# Run all scenarios under twister (compile on QEMU, real numbers on hardware):
west twister -T tests/benchmarks/arm_latency -p <board>
```
