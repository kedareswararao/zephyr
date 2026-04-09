/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Kernel Operations PMU Benchmark Suite
 *
 * Uses the Zephyr @c pmu_* API (hardware event counters) to measure the cost of
 * fundamental kernel operations.  Unlike the latency_measure benchmark (which uses
 * timing_timestamp_get() for wall-clock cycles only), this suite instruments
 * operations with PMU events to provide richer architectural insight:
 * instructions retired, exceptions taken, cache refills, and branch
 * mispredictions alongside raw cycle counts.
 *
 * Test categories:
 * 1. Context switch (k_yield ping-pong)
 * 2. Interrupt latency (irq_offload)
 * 3. Thread suspend / resume
 * 4. Thread create / abort
 * 5. Syscall overhead (k_uptime_get)
 * 6. Syscall overhead (semaphore give / take)
 *
 * All tests use only architectural PMU events (0x00-0x1F) for portability
 * across backends that implement @c PMU_EVT_* as Arm architectural codes.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/pmu.h>
#include <zephyr/irq_offload.h>
#include <string.h>

#ifdef CONFIG_SMP
BUILD_ASSERT(IS_ENABLED(CONFIG_SCHED_CPU_MASK),
	     "SMP build requires CONFIG_SCHED_CPU_MASK for CPU pinning; "
	     "PMU registers are per-CPU and measurements are invalid if the "
	     "test thread migrates between CPUs during a benchmark run.");
#endif

/* Iteration counts */
#define NUM_ITERATIONS          1000
#define THREAD_CREATE_ITERATIONS 500  /* Thread create/abort is heavyweight */

/* Thread configuration */
#define HELPER_STACK_SIZE       1024
#define BENCH_PRIORITY          K_PRIO_COOP(7)

/* Helper macros — same pattern as cpu_performance */
#define MEASURE_START() \
	pmu_counter_reset_all(); \
	pmu_cycle_reset(); \
	pmu_start()

#define MEASURE_STOP() pmu_stop()

static void print_pm_result(const char *test_name,
			    const struct pmu_counter_config *cfgs,
			    uint64_t cycles,
			    uint64_t e0, uint64_t e1, uint64_t e2, uint64_t e3)
{
	TC_PRINT("%-30s: %10llu cycles", test_name, (unsigned long long)cycles);
	if (e0 > 0U) {
		TC_PRINT(", %s=%llu", pmu_event_name(cfgs[0].event),
			 (unsigned long long)e0);
	}
	if (e1 > 0U) {
		TC_PRINT(", %s=%llu", pmu_event_name(cfgs[1].event),
			 (unsigned long long)e1);
	}
	if (e2 > 0U) {
		TC_PRINT(", %s=%llu", pmu_event_name(cfgs[2].event),
			 (unsigned long long)e2);
	}
	if (e3 > 0U) {
		TC_PRINT(", %s=%llu", pmu_event_name(cfgs[3].event),
			 (unsigned long long)e3);
	}
	TC_PRINT("\n");
}

#define PRINT_AVG(label, total, n) \
	TC_PRINT("  %-26s: %llu (avg per op)\n", label, \
		 (unsigned long long)((total) / (n)))

/*
 * Counter configurations for each test category.
 *
 * Context Switch: instruction flow + cache pressure from stack switching.
 * Interrupt:      exception entry/return path.
 * Thread ops:     memory-intensive (struct init, stack setup).
 * Syscall:        instruction + exception flow.
 */
static struct pmu_counter_config ctx_switch_counters[] = {
	{.event = PMU_EVT_INST_RETIRED,      .enabled = true},
	{.event = PMU_EVT_EXC_TAKEN,         .enabled = true},
	{.event = PMU_EVT_L1D_CACHE_REFILL,  .enabled = true},
	{.event = PMU_EVT_BR_MIS_PRED,       .enabled = true},
};

static struct pmu_counter_config irq_counters[] = {
	{.event = PMU_EVT_INST_RETIRED,      .enabled = true},
	{.event = PMU_EVT_EXC_TAKEN,         .enabled = true},
	{.event = PMU_EVT_EXC_RETURN,        .enabled = true},
	{.event = PMU_EVT_BR_MIS_PRED,       .enabled = true},
};

static struct pmu_counter_config thread_counters[] = {
	{.event = PMU_EVT_INST_RETIRED,      .enabled = true},
	{.event = PMU_EVT_MEM_ACCESS,        .enabled = true},
	{.event = PMU_EVT_L1D_CACHE_REFILL,  .enabled = true},
	{.event = PMU_EVT_BR_MIS_PRED,       .enabled = true},
};

static struct pmu_counter_config syscall_counters[] = {
	{.event = PMU_EVT_INST_RETIRED,      .enabled = true},
	{.event = PMU_EVT_EXC_TAKEN,         .enabled = true},
	{.event = PMU_EVT_EXC_RETURN,        .enabled = true},
	{.event = PMU_EVT_CPU_CYCLES,        .enabled = true},
};

/* Mutable copy passed to pmu_configure_counters() */
static struct pmu_counter_config counter_configs[4];

/* Shared thread resources */
static struct k_thread helper_thread;
static K_THREAD_STACK_DEFINE(helper_stack, HELPER_STACK_SIZE);
static struct k_sem sync_sem;

/* Volatile counter for ISR body */
static volatile uint32_t isr_count;

/* ------------------------------------------------------------------ */
/* Suite setup                                                         */
/* ------------------------------------------------------------------ */

static void test_setup(void)
{
	uint32_t implementer, idcode;
	int ret;

	ret = pmu_init();

	TC_PRINT("\n");
	TC_PRINT("=================================================================\n");
	TC_PRINT("ARM64 Kernel Operations PMU Benchmark Suite\n");
	TC_PRINT("=================================================================\n");

	if (ret != 0) {
		TC_PRINT("WARNING: PMU initialization failed (ret=%d)\n", ret);
		TC_PRINT("This is common on QEMU - tests will be skipped.\n");
		TC_PRINT("=================================================================\n\n");
		return;
	}

	TC_PRINT("PMU initialized: %u counters available\n",
		 pmu_num_counters());

	pmu_get_info(&implementer, &idcode);
	TC_PRINT("CPU Implementer: 0x%02x, ID Code: 0x%02x\n",
		 implementer, idcode);
	TC_PRINT("CPU Frequency: %u MHz (runtime calibrated)\n",
		 pmu_cpu_freq_mhz());

	k_sem_init(&sync_sem, 0, 1);

	TC_PRINT("=================================================================\n\n");
}

static void *pmu_bench_setup(void)
{
	/*
	 * The test thread runs at cooperative priority and will not be
	 * preempted or migrated to another CPU.  Helper threads are
	 * explicitly pinned to CPU 0 before k_thread_start().
	 */
	test_setup();
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Test 01: Context Switch via k_yield() ping-pong                     */
/* ------------------------------------------------------------------ */

static void yield_helper_entry(void *p1, void *p2, void *p3)
{
	uint32_t n = (uint32_t)(uintptr_t)p1;
	struct k_sem *ready = (struct k_sem *)p2;

	/* Signal that we are ready */
	k_sem_give(ready);

	for (uint32_t i = 0; i < n; i++) {
		k_yield();
	}
}

ZTEST(pmu_benchmarks, test_01_context_switch_yield)
{
	uint64_t cycles, c0, c1, c2, c3;
	uint32_t total_switches;

	if (pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 1: Context Switch (k_yield ping-pong) ---\n");

	memcpy(counter_configs, ctx_switch_counters, sizeof(ctx_switch_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	/* Create helper at same cooperative priority.
	 * Use K_FOREVER + explicit start so we can pin the thread to CPU 0
	 * on SMP before it begins running.
	 */
	k_thread_create(&helper_thread, helper_stack,
			K_THREAD_STACK_SIZEOF(helper_stack),
			yield_helper_entry,
			(void *)(uintptr_t)NUM_ITERATIONS,
			&sync_sem, NULL,
			BENCH_PRIORITY, 0, K_FOREVER);
#ifdef CONFIG_SMP
	k_thread_cpu_pin(&helper_thread, 0);
#endif
	k_thread_start(&helper_thread);

	/* Wait for helper to be ready */
	k_sem_take(&sync_sem, K_FOREVER);

	MEASURE_START();

	for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
		k_yield();
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);  /* INST_RETIRED */
	c1 = pmu_counter_read(1);  /* EXC_TAKEN */
	c2 = pmu_counter_read(2);  /* L1D_CACHE_REFILL */
	c3 = pmu_counter_read(3);  /* BR_MIS_PRED */

	/* Each loop iteration = 2 context switches (main->helper, helper->main) */
	total_switches = NUM_ITERATIONS * 2;

	print_pm_result("Context Switch (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	PRINT_AVG("Cycles/switch", cycles, total_switches);
	PRINT_AVG("Instructions/switch", c0, total_switches);

	k_thread_abort(&helper_thread);
}

/* ------------------------------------------------------------------ */
/* Test 02: Interrupt Latency via irq_offload()                        */
/* ------------------------------------------------------------------ */

static void minimal_isr(const void *arg)
{
	ARG_UNUSED(arg);
	isr_count++;
}

ZTEST(pmu_benchmarks, test_02_interrupt_latency)
{
	uint64_t cycles, c0, c1, c2, c3;

	if (pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 2: Interrupt Latency (irq_offload) ---\n");

	memcpy(counter_configs, irq_counters, sizeof(irq_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	isr_count = 0;

	MEASURE_START();

	for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
		irq_offload(minimal_isr, NULL);
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);  /* INST_RETIRED */
	c1 = pmu_counter_read(1);  /* EXC_TAKEN */
	c2 = pmu_counter_read(2);  /* EXC_RETURN */
	c3 = pmu_counter_read(3);  /* BR_MIS_PRED */

	print_pm_result("Interrupt Latency (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	PRINT_AVG("Cycles/interrupt", cycles, NUM_ITERATIONS);
	PRINT_AVG("Instructions/interrupt", c0, NUM_ITERATIONS);

	TC_PRINT("  EXC_TAKEN count         : %llu (expected ~%u)\n",
		 c1, NUM_ITERATIONS);
	TC_PRINT("  EXC_RETURN count        : %llu (expected ~%u)\n",
		 c2, NUM_ITERATIONS);

	zassert_equal(isr_count, NUM_ITERATIONS,
		      "ISR ran %u times, expected %u", isr_count, NUM_ITERATIONS);
}

/* ------------------------------------------------------------------ */
/* Test 03: Thread Suspend / Resume                                    */
/* ------------------------------------------------------------------ */

static void suspend_resume_helper(void *p1, void *p2, void *p3)
{
	struct k_sem *ready = (struct k_sem *)p1;

	/* Signal that we are running */
	k_sem_give(ready);

	/* Repeatedly suspend self; bench thread resumes us each time */
	while (1) {
		k_thread_suspend(k_current_get());
	}
}

ZTEST(pmu_benchmarks, test_03_thread_suspend_resume)
{
	uint64_t cycles, c0, c1, c2, c3;

	if (pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 3: Thread Suspend / Resume ---\n");

	memcpy(counter_configs, thread_counters, sizeof(thread_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	/*
	 * Create helper at higher priority (lower numeric value).
	 * Use K_FOREVER + explicit start to pin to CPU 0 on SMP first.
	 * When resumed, it runs immediately, then suspends itself,
	 * returning control to the bench thread.
	 */
	k_thread_create(&helper_thread, helper_stack,
			K_THREAD_STACK_SIZEOF(helper_stack),
			suspend_resume_helper,
			&sync_sem, NULL, NULL,
			BENCH_PRIORITY - 1, 0, K_FOREVER);
#ifdef CONFIG_SMP
	k_thread_cpu_pin(&helper_thread, 0);
#endif
	k_thread_start(&helper_thread);

	/* Wait for helper to be running and then suspend itself */
	k_sem_take(&sync_sem, K_FOREVER);
	/*
	 * At this point helper has called k_sem_give and looped back
	 * to k_thread_suspend.  It is now suspended.
	 */

	MEASURE_START();

	for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
		/* Resume helper -> it runs -> suspends self -> we continue */
		k_thread_resume(&helper_thread);
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);  /* INST_RETIRED */
	c1 = pmu_counter_read(1);  /* MEM_ACCESS */
	c2 = pmu_counter_read(2);  /* L1D_CACHE_REFILL */
	c3 = pmu_counter_read(3);  /* BR_MIS_PRED */

	print_pm_result("Suspend/Resume (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	/* Each iteration = 1 resume + 1 suspend (2 context switches) */
	PRINT_AVG("Cycles/resume-suspend", cycles, NUM_ITERATIONS);
	PRINT_AVG("Instructions/resume-suspend", c0, NUM_ITERATIONS);

	k_thread_abort(&helper_thread);
}

/* ------------------------------------------------------------------ */
/* Test 04: Thread Create / Abort                                      */
/* ------------------------------------------------------------------ */

static void create_abort_helper(void *p1, void *p2, void *p3)
{
	struct k_sem *done = (struct k_sem *)p1;

	/* Signal completion, then exit naturally */
	k_sem_give(done);
}

ZTEST(pmu_benchmarks, test_04_thread_create_abort)
{
	uint64_t cycles, c0, c1, c2, c3;

	if (pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 4: Thread Create / Abort ---\n");

	memcpy(counter_configs, thread_counters, sizeof(thread_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	MEASURE_START();

	for (uint32_t i = 0; i < THREAD_CREATE_ITERATIONS; i++) {
		k_sem_reset(&sync_sem);

		/*
		 * Create at higher priority so k_thread_start() immediately
		 * context-switches to the helper.  Helper signals sync_sem
		 * and exits.
		 */
		k_thread_create(&helper_thread, helper_stack,
				K_THREAD_STACK_SIZEOF(helper_stack),
				create_abort_helper,
				&sync_sem, NULL, NULL,
				BENCH_PRIORITY - 1, 0, K_FOREVER);
#ifdef CONFIG_SMP
		k_thread_cpu_pin(&helper_thread, 0);
#endif
		k_thread_start(&helper_thread);

		/* Helper has run and exited; wait for the signal */
		k_sem_take(&sync_sem, K_FOREVER);

		/* Clean up thread resources */
		k_thread_abort(&helper_thread);
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);  /* INST_RETIRED */
	c1 = pmu_counter_read(1);  /* MEM_ACCESS */
	c2 = pmu_counter_read(2);  /* L1D_CACHE_REFILL */
	c3 = pmu_counter_read(3);  /* BR_MIS_PRED */

	print_pm_result("Create/Abort (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	PRINT_AVG("Cycles/create-abort", cycles, THREAD_CREATE_ITERATIONS);
	PRINT_AVG("Instructions/create-abort", c0, THREAD_CREATE_ITERATIONS);
}

/* ------------------------------------------------------------------ */
/* Test 05: Syscall — k_uptime_get()                                   */
/* ------------------------------------------------------------------ */

ZTEST(pmu_benchmarks, test_05_syscall_uptime)
{
	volatile int64_t t;
	uint64_t cycles, c0, c1, c2, c3;

	if (pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 5: Syscall Overhead (k_uptime_get) ---\n");

	memcpy(counter_configs, syscall_counters, sizeof(syscall_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	MEASURE_START();

	for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
		t = k_uptime_get();
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);  /* INST_RETIRED */
	c1 = pmu_counter_read(1);  /* EXC_TAKEN */
	c2 = pmu_counter_read(2);  /* EXC_RETURN */
	c3 = pmu_counter_read(3);  /* CPU_CYCLES */

	print_pm_result("k_uptime_get (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	PRINT_AVG("Cycles/call", cycles, NUM_ITERATIONS);
	PRINT_AVG("Instructions/call", c0, NUM_ITERATIONS);

	TC_PRINT("  EXC_TAKEN               : %llu (expect ~0 in kernel mode)\n",
		 c1);

	/* Prevent compiler from optimizing out the loop */
	zassert_true(t >= 0, "Uptime should be non-negative");
}

/* ------------------------------------------------------------------ */
/* Test 06: Syscall — Semaphore give / take                            */
/* ------------------------------------------------------------------ */

ZTEST(pmu_benchmarks, test_06_syscall_sem_give_take)
{
	struct k_sem bench_sem;
	uint64_t cycles, c0, c1, c2, c3;

	if (pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 6: Syscall Overhead (sem give / take) ---\n");

	/* --- Measure k_sem_give (uncontested, no waiters) --- */

	k_sem_init(&bench_sem, 0, NUM_ITERATIONS);

	memcpy(counter_configs, syscall_counters, sizeof(syscall_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	MEASURE_START();

	for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
		k_sem_give(&bench_sem);
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);
	c1 = pmu_counter_read(1);
	c2 = pmu_counter_read(2);
	c3 = pmu_counter_read(3);

	print_pm_result("k_sem_give (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	PRINT_AVG("Cycles/give", cycles, NUM_ITERATIONS);
	PRINT_AVG("Instructions/give", c0, NUM_ITERATIONS);

	/* --- Measure k_sem_take (immediate, no blocking) --- */

	/* Semaphore count is now NUM_ITERATIONS from the gives above */
	memcpy(counter_configs, syscall_counters, sizeof(syscall_counters));
	if (pmu_configure_counters(counter_configs, 4) != 0) {
		TC_PRINT("  Skipped: fewer than 4 counters available\n");
		ztest_test_skip();
		return;
	}

	MEASURE_START();

	for (uint32_t i = 0; i < NUM_ITERATIONS; i++) {
		k_sem_take(&bench_sem, K_NO_WAIT);
	}

	MEASURE_STOP();

	cycles = pmu_cycle_count();
	c0 = pmu_counter_read(0);
	c1 = pmu_counter_read(1);
	c2 = pmu_counter_read(2);
	c3 = pmu_counter_read(3);

	print_pm_result("k_sem_take (total)", counter_configs,
		     cycles, c0, c1, c2, c3);

	PRINT_AVG("Cycles/take", cycles, NUM_ITERATIONS);
	PRINT_AVG("Instructions/take", c0, NUM_ITERATIONS);
}

/* ------------------------------------------------------------------ */
/* Summary                                                             */
/* ------------------------------------------------------------------ */

ZTEST(pmu_benchmarks, test_99_summary)
{
	TC_PRINT("\n");
	TC_PRINT("=================================================================\n");
	TC_PRINT("Benchmark Suite Complete\n");
	TC_PRINT("=================================================================\n");
	TC_PRINT("\n");
	TC_PRINT("PMU event counters provide architectural insight beyond raw\n");
	TC_PRINT("cycle counts: instructions retired, exceptions taken, cache\n");
	TC_PRINT("refills, and branch mispredictions for each kernel operation.\n");
	TC_PRINT("\n");
	TC_PRINT("Compare results across Cortex-A53, A72, and A78 to understand\n");
	TC_PRINT("how microarchitecture differences affect OS overhead.\n");
	TC_PRINT("=================================================================\n");
}

ZTEST_SUITE(pmu_benchmarks, NULL, pmu_bench_setup, NULL, NULL, NULL);
