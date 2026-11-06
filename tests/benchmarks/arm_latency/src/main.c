/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ARM Cortex-A/R latency micro-benchmark (apples-to-apples with the FreeRTOS
 * perf numbers).
 *
 * Measures, in CPU cycles via the PMU cycle counter (PMCCNTR_EL0 on A-profile,
 * the CP15 PMU cycle counter on Cortex-R - the same counters FreeRTOS uses),
 * two quantities:
 *
 *   1. Task context-switch time: two equal-priority threads pinned to one CPU
 *      ping-pong with k_yield(). The yielder records the cycle count just
 *      before yielding; the catcher records it as the first thing on entry.
 *      The difference is one yield-driven context switch - the direct analogue
 *      of the FreeRTOS taskYIELD() measurement.
 *
 *   2. Interrupt latency: arm the cycle counter, read it, raise a GIC SGI to
 *      this CPU, and read the counter again as the first instruction of the
 *      ISR. The difference is the trigger->ISR-entry latency - the direct
 *      analogue of the FreeRTOS SGI interrupt-latency measurement.
 *
 * Everything runs pinned to CPU 0 so a yield forces a real context switch and
 * the SGI is taken on the measuring CPU. Build single-core (non-SMP) to match
 * FreeRTOS exactly; on an SMP board the pinning keeps the measurement on one
 * core but the scheduler carries a little more state.
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/interrupt_controller/gic.h>

/*
 * Cycle counter and SGI helpers, abstracted per architecture:
 *   - arm64 (Cortex-A): PMUv3 PMCCNTR_EL0 via <zephyr/arch/arm64/pmuv3.h>.
 *   - Cortex-R (Armv7-R/Armv8-R AArch32): the CP15 PMU cycle counter - the same
 *     counter and enable sequence the FreeRTOS R5 benchmark uses (PMCNTENSET
 *     bit 31, PMCR divider off, read PMCCNTR at CP15 c9,c13,0).
 */
#if defined(CONFIG_ARM64)

#include <zephyr/arch/arm64/cpu.h>
#include <zephyr/arch/arm64/pmuv3.h>

static inline int bench_cycle_init(void)
{
	if (pmu_init() != 0) {
		return -1;
	}
	pmu_start();
	pmu_cycle_reset();
	return 0;
}

static inline uint64_t bench_cycle_count(void)
{
	return pmu_cycle_count();
}

static inline uint32_t bench_cpu_mhz_arch(void)
{
	return arch_pmu_cpu_freq_mhz();
}

static inline int bench_cpu_id(void)
{
	return (int)arch_curr_cpu()->id;
}

static inline void bench_raise_sgi(unsigned int sgi)
{
	uint64_t mpidr = GET_MPIDR();
	uint8_t aff0 = (uint8_t)MPIDR_AFFLVL(mpidr, 0);

	gic_raise_sgi(sgi, mpidr, (uint16_t)BIT(aff0));
}

#define BENCH_IRQ_PRIO IRQ_DEFAULT_PRIORITY

#elif defined(CONFIG_CPU_AARCH32_CORTEX_R)

#include <zephyr/arch/arm/cortex_a_r/lib_helpers.h>

/* CP15 PMU register access: read/write_sysreg32(op1, CRn, CRm, op2). */
#define PMU_PMCR(v)       write_sysreg32((v), 0, 9, 12, 0)
#define PMU_PMCR_RD()     read_sysreg32(0, 9, 12, 0)
#define PMU_PMCNTENSET(v) write_sysreg32((v), 0, 9, 12, 1)
#define PMU_PMCCNTR_RD()  read_sysreg32(0, 9, 13, 0)

#define PMU_PMCR_E        BIT(0)  /* enable all counters */
#define PMU_PMCR_C        BIT(2)  /* reset cycle counter */
#define PMU_PMCR_D        BIT(3)  /* cycle count divider (/64) */
#define PMU_CCNT_EN       BIT(31) /* cycle counter in PMCNTENSET */

static uint32_t bench_r_mhz;

static inline int bench_cycle_init(void)
{
	uint32_t pmcr = PMU_PMCR_RD();

	pmcr |= PMU_PMCR_E | PMU_PMCR_C;
	pmcr &= ~PMU_PMCR_D;    /* count every CPU cycle, like FreeRTOS */
	PMU_PMCR(pmcr);
	PMU_PMCNTENSET(PMU_CCNT_EN);
	return 0;
}

static inline uint64_t bench_cycle_count(void)
{
	return (uint64_t)PMU_PMCCNTR_RD();
}

static inline uint32_t bench_cpu_mhz_arch(void)
{
	/*
	 * No runtime PMU freq helper on Cortex-R: calibrate the CP15 cycle
	 * counter against the system timer once. This is only as accurate as the
	 * system timer rate, so it relies on the board's timer clock-frequency
	 * being correct; the raw cycle counts are unaffected either way.
	 */
	if (bench_r_mhz == 0U) {
		uint32_t c0 = (uint32_t)bench_cycle_count();

		k_busy_wait(10000);
		uint32_t c1 = (uint32_t)bench_cycle_count();

		bench_r_mhz = (uint32_t)((c1 - c0) / 10000U);
	}
	return bench_r_mhz;
}

static inline int bench_cpu_id(void)
{
	return 0;
}

static inline void bench_raise_sgi(unsigned int sgi)
{
	/* Single-core RPU: target CPU interface 0. */
	gic_raise_sgi(sgi, 0, (uint16_t)BIT(0));
}

/* AArch32 GIC has no IRQ_DEFAULT_PRIORITY; pick a mid priority for the SGI. */
#define BENCH_IRQ_PRIO 1

#else
#error "arm_latency supports arm64 (PMUv3) and Cortex-R (CP15 PMU) only"
#endif

#define BENCH_CPU    0
#define CS_ITERS     1000
#define IRQ_ITERS    1000
#define BENCH_STACK  2048

/*
 * SGIs 0-3 are reserved by the kernel. On GICv2 under a secure monitor (BL31)
 * the ARM/Xilinx convention keeps SGIs 8-15 as Group 0 (secure), undeliverable
 * to Zephyr running non-secure, and leaves 0-7 as Group 1 (NS). SGI 7 is the
 * one choice that is both non-reserved and non-secure, and it is also valid on
 * GICv3/GICv1 targets.
 */
#define BENCH_SGI    7

/*
 * Bound the wait for the self-SGI so a target that does not deliver it (for
 * example a GICv2 under a secure monitor, where a non-secure self-SGI may not
 * reach NS-EL1) reports and moves on instead of hanging forever. Far larger
 * than any real trigger->ISR latency, so it never trips on a working target.
 */
#define BENCH_IRQ_GUARD  10000000U

/*
 * Pin to one CPU where that is meaningful. k_thread_cpu_pin() only exists with
 * CONFIG_SCHED_CPU_MASK (which requires SMP); a single-core build has just one
 * CPU, so pinning is a no-op there.
 */
static inline void bench_pin(k_tid_t tid, int cpu)
{
#if defined(CONFIG_SCHED_CPU_MASK)
	(void)k_thread_cpu_pin(tid, cpu);
#else
	ARG_UNUSED(tid);
	ARG_UNUSED(cpu);
#endif
}

/*
 * Dirty the FP/SIMD register file so the thread becomes the FPU owner. Zephyr
 * arm64 switches FP lazily: the context switch itself only flips CPACR access,
 * and the actual save of the previous owner plus restore of the new one happens
 * on the first FP access after the switch, as a trap. To get a context-switch
 * number comparable to FreeRTOS - whose switch saves/restores Q0-Q31 eagerly -
 * each thread touches FP before the measurement point so the trap (and thus the
 * save+restore cost) lands inside the measured window.
 *
 * When CONFIG_FPU is not set this is a true no-op: no soft-float is emitted and
 * the integer-only switch number is left untouched.
 */
#if defined(CONFIG_FPU)
static volatile double bench_fp_acc = 1.0;

static void bench_touch_fp(void)
{
	double a = bench_fp_acc;

	a = (a * 1.0000001) + 0.5;
	a = a - 0.25;
	bench_fp_acc = a;
}
#else
static inline void bench_touch_fp(void) { }
#endif

static K_THREAD_STACK_DEFINE(ctrl_stack, BENCH_STACK);
static K_THREAD_STACK_DEFINE(yielder_stack, BENCH_STACK);
static K_THREAD_STACK_DEFINE(catcher_stack, BENCH_STACK);
static struct k_thread ctrl_thread;
static struct k_thread yielder_thread;
static struct k_thread catcher_thread;

/* Context-switch measurement state. */
static volatile uint64_t cs_t0;
static volatile uint32_t cs_stop;
static uint64_t cs_min = UINT64_MAX;
static uint64_t cs_max;
static uint64_t cs_sum;
static uint32_t cs_cnt;
static struct k_sem cs_fin;

/* Interrupt-latency measurement state. */
static volatile uint64_t irq_t_start;
static volatile uint64_t irq_t_isr;
static volatile uint32_t irq_done;

static void bench_sgi_isr(const void *arg)
{
	/* First instruction in the ISR: capture the cycle counter. */
	irq_t_isr = bench_cycle_count();
	ARG_UNUSED(arg);
	irq_done = 1U;
}

static void cs_yielder(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (cs_stop == 0U) {
		/* Become the FPU owner before yielding (see bench_touch_fp). */
		bench_touch_fp();
		cs_t0 = bench_cycle_count();
		k_yield();
	}
}

static void cs_catcher(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (int i = 0; i < CS_ITERS; i++) {
		/*
		 * Touch FP first: with CONFIG_FPU this traps and forces the
		 * save of the yielder's FP context plus restore of ours, so the
		 * stamp below captures the full switch cost. Without CONFIG_FPU
		 * it is a no-op and t1 is stamped immediately on entry.
		 */
		bench_touch_fp();
		uint64_t t1 = bench_cycle_count();
		/* 32-bit-safe: the R5 CP15 counter is 32-bit and may wrap. */
		uint64_t d = (uint32_t)(t1 - cs_t0);

		/* Skip the first couple: the ping-pong is not yet in steady state. */
		if (i >= 2) {
			cs_sum += d;
			cs_cnt++;
			if (d < cs_min) {
				cs_min = d;
			}
			if (d > cs_max) {
				cs_max = d;
			}
		}
		k_yield();
	}

	cs_stop = 1U;
	k_sem_give(&cs_fin);
}

static void measure_context_switch(void)
{
	k_tid_t ty;
	k_tid_t tc;

	cs_min = UINT64_MAX;
	cs_max = 0U;
	cs_sum = 0U;
	cs_cnt = 0U;
	cs_stop = 0U;
	cs_t0 = bench_cycle_count();

	ty = k_thread_create(&yielder_thread, yielder_stack, BENCH_STACK,
			     cs_yielder, NULL, NULL, NULL,
			     K_PRIO_PREEMPT(5), 0, K_FOREVER);
	bench_pin(ty, BENCH_CPU);

	tc = k_thread_create(&catcher_thread, catcher_stack, BENCH_STACK,
			     cs_catcher, NULL, NULL, NULL,
			     K_PRIO_PREEMPT(5), 0, K_FOREVER);
	bench_pin(tc, BENCH_CPU);

	k_thread_start(ty);
	k_thread_start(tc);

	/* The controller steps aside (blocks) so CPU 0 is free to ping-pong. */
	(void)k_sem_take(&cs_fin, K_FOREVER);
	(void)k_thread_join(&catcher_thread, K_FOREVER);
	(void)k_thread_join(&yielder_thread, K_FOREVER);

	if (cs_cnt > 0U) {
		uint32_t avg = (uint32_t)(cs_sum / cs_cnt);

		printk("CONTEXT_SWITCH: iters=%u min=%u avg=%u max=%u cycles\n",
		       cs_cnt, (uint32_t)cs_min, avg, (uint32_t)cs_max);
	} else {
		printk("CONTEXT_SWITCH: no samples\n");
	}
}

static void measure_irq_latency(void)
{
	uint64_t irq_min = UINT64_MAX;
	uint64_t irq_max = 0U;
	uint64_t irq_sum = 0U;
	uint32_t irq_cnt = 0U;

	irq_connect_dynamic(BENCH_SGI, BENCH_IRQ_PRIO, bench_sgi_isr,
			    NULL, 0);
	irq_enable(BENCH_SGI);

	for (int i = 0; i < IRQ_ITERS; i++) {
		irq_done = 0U;
		barrier_isync_fence_full();

		irq_t_start = bench_cycle_count();
		bench_raise_sgi(BENCH_SGI);

		uint32_t guard = 0U;

		while (irq_done == 0U) {
			/* spin until the ISR records its entry cycle */
			if (++guard >= BENCH_IRQ_GUARD) {
				break;
			}
		}

		if (irq_done == 0U) {
			/*
			 * SGI not delivered to this context (e.g. GICv2 self-SGI
			 * under a secure monitor). Report and stop rather than
			 * hang; the context-switch result above is still valid.
			 */
			printk("IRQ_LATENCY: SGI %u not delivered (guard timeout); "
			       "interrupt-latency not measured on this target\n",
			       BENCH_SGI);
			irq_disable(BENCH_SGI);
			return;
		}

		uint64_t d = (uint32_t)(irq_t_isr - irq_t_start);

		if (i >= 2) {
			irq_sum += d;
			irq_cnt++;
			if (d < irq_min) {
				irq_min = d;
			}
			if (d > irq_max) {
				irq_max = d;
			}
		}
	}

	irq_disable(BENCH_SGI);

	if (irq_cnt > 0U) {
		uint32_t avg = (uint32_t)(irq_sum / irq_cnt);

		printk("IRQ_LATENCY: iters=%u min=%u avg=%u max=%u cycles\n",
		       irq_cnt, (uint32_t)irq_min, avg, (uint32_t)irq_max);
	} else {
		printk("IRQ_LATENCY: no samples\n");
	}
}

static void bench_control(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	printk("latency benchmark: CPU %d, measured CPU freq %u MHz, "
	       "fp_switch=%d\n", bench_cpu_id(), bench_cpu_mhz_arch(),
	       IS_ENABLED(CONFIG_FPU));

	measure_context_switch();
	measure_irq_latency();

	printk("latency benchmark: done\n");
}

int main(void)
{
	k_tid_t ctrl;

	printk("\n*** ARM Cortex-A/R latency micro-benchmark ***\n");

	if (bench_cycle_init() != 0) {
		printk("ERROR: cycle counter not available (arm64 needs "
		       "CONFIG_ARM64_PMUV3 and PMU visible at this EL)\n");
		return 0;
	}

	k_sem_init(&cs_fin, 0, 1);

	/* Run the whole benchmark pinned to one CPU. */
	ctrl = k_thread_create(&ctrl_thread, ctrl_stack, BENCH_STACK,
			       bench_control, NULL, NULL, NULL,
			       K_PRIO_PREEMPT(2), 0, K_FOREVER);
	bench_pin(ctrl, BENCH_CPU);
	k_thread_start(ctrl);
	(void)k_thread_join(&ctrl_thread, K_FOREVER);

	return 0;
}
