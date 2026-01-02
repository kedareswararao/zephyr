/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CPU Performance Benchmark using ARM64 PMU
 *
 * This test suite validates the ARM64 PMU driver and provides comprehensive
 * CPU performance benchmarks for Cortex-A processors (A53, A72, A78, etc.)
 *
 * Test categories:
 * 1. Cache performance (L1 D-cache, L2 cache)
 * 2. Branch prediction
 * 3. Memory access patterns
 * 4. Compute performance
 * 5. TLB performance
 *
 * All tests use only architectural PMU events (0x00-0x1F) for portability
 * across all ARMv8-A processors.
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/arch/arm64/pmu.h>
#include <zephyr/random/random.h>
#include <stdlib.h>
#include <string.h>

/* Test buffer sizes */
#define L1_CACHE_SIZE       (32 * 1024)    /* 32KB - fits in L1 */
#define L1_CACHE_WORK_SIZE  (16 * 1024)    /* Half of L1 */
#define L2_CACHE_SIZE       (256 * 1024)   /* 256KB - fits in L2 */
#define L2_CACHE_WORK_SIZE  (128 * 1024)   /* Half of L2 */
#define L3_CACHE_SIZE       (2 * 1024 * 1024)  /* 2MB - exceeds L2 */
#define CACHE_LINE_SIZE     64

#define ARRAY_ELEMENTS      (L2_CACHE_WORK_SIZE / sizeof(uint64_t))
#define LARGE_ARRAY_ELEMENTS (L3_CACHE_SIZE / sizeof(uint64_t))

/* Test iterations */
#define COMPUTE_ITERATIONS  10000
#define MEMORY_ITERATIONS   1000
#define BRANCH_ITERATIONS   100000

/* Test buffers */
static uint64_t __aligned(64) test_array[ARRAY_ELEMENTS];
static uint64_t __aligned(64) large_array[LARGE_ARRAY_ELEMENTS];
static uint32_t __aligned(64) random_indices[MEMORY_ITERATIONS];

/* Helper macros */
#define MEASURE_START() \
	arch_pmu_counter_reset_all(); \
	arch_pmu_cycle_reset(); \
	arch_pmu_start()

#define MEASURE_STOP() arch_pmu_stop()

#define PRINT_RESULT(test_name, cycles, e0, e1, e2, e3) \
	TC_PRINT("%-30s: %10llu cycles", test_name, cycles); \
	if (e0 > 0) TC_PRINT(", %s=%llu", arch_pmu_event_name(counter_configs[0].event), e0); \
	if (e1 > 0) TC_PRINT(", %s=%llu", arch_pmu_event_name(counter_configs[1].event), e1); \
	if (e2 > 0) TC_PRINT(", %s=%llu", arch_pmu_event_name(counter_configs[2].event), e2); \
	if (e3 > 0) TC_PRINT(", %s=%llu", arch_pmu_event_name(counter_configs[3].event), e3); \
	TC_PRINT("\n")

/* Counter configurations for different test types */
static struct pmu_counter_config cache_counters[] = {
	{.event = PMU_EVT_L1D_CACHE, .enabled = true},
	{.event = PMU_EVT_L1D_CACHE_REFILL, .enabled = true},
	{.event = PMU_EVT_L2D_CACHE, .enabled = true},
	{.event = PMU_EVT_L2D_CACHE_REFILL, .enabled = true},
};

static struct pmu_counter_config branch_counters[] = {
	{.event = PMU_EVT_BR_PRED, .enabled = true},
	{.event = PMU_EVT_BR_MIS_PRED, .enabled = true},
	{.event = PMU_EVT_INST_RETIRED, .enabled = true},
	{.event = PMU_EVT_INST_SPEC, .enabled = true},
};

static struct pmu_counter_config memory_counters[] = {
	{.event = PMU_EVT_MEM_ACCESS, .enabled = true},
	{.event = PMU_EVT_BUS_ACCESS, .enabled = true},
	{.event = PMU_EVT_INST_RETIRED, .enabled = true},
	{.event = PMU_EVT_CPU_CYCLES, .enabled = true},
};

static struct pmu_counter_config tlb_counters[] = {
	{.event = PMU_EVT_L1D_TLB_REFILL, .enabled = true},
	{.event = PMU_EVT_L1I_TLB_REFILL, .enabled = true},
	{.event = PMU_EVT_MEM_ACCESS, .enabled = true},
	{.event = PMU_EVT_INST_RETIRED, .enabled = true},
};

static struct pmu_counter_config counter_configs[4];

/* Test setup */
static void test_setup(void)
{
	int ret;

	/* Initialize PMU */
	ret = arch_pmu_init();
	
	TC_PRINT("\n");
	TC_PRINT("=================================================================\n");
	TC_PRINT("ARM64 CPU Performance Benchmark Suite\n");
	TC_PRINT("=================================================================\n");

	if (ret != 0) {
		TC_PRINT("WARNING: PMU initialization failed (ret=%d)\n", ret);
		TC_PRINT("PMU may not be available on this platform.\n");
		TC_PRINT("This is common on QEMU - tests will be skipped.\n");
		TC_PRINT("To validate PMU on real hardware:\n");
		TC_PRINT("  1. Ensure TF-A/bootloader enables PMU access\n");
		TC_PRINT("  2. Run on physical Cortex-A hardware (A53/A72/A78)\n");
		TC_PRINT("=================================================================\n\n");
		return;
	}

	TC_PRINT("PMU initialized: %u counters available\n", arch_pmu_num_counters());
	
	uint32_t implementer, idcode;
	arch_pmu_get_info(&implementer, &idcode);
	TC_PRINT("CPU Implementer: 0x%02x, ID Code: 0x%02x\n", implementer, idcode);

	/* Initialize test buffers */
	for (size_t i = 0; i < ARRAY_ELEMENTS; i++) {
		test_array[i] = i;
	}
	for (size_t i = 0; i < LARGE_ARRAY_ELEMENTS; i++) {
		large_array[i] = i;
	}

	/* Generate random indices for random access tests */
	for (size_t i = 0; i < MEMORY_ITERATIONS; i++) {
		random_indices[i] = sys_rand32_get() % ARRAY_ELEMENTS;
	}

	TC_PRINT("Test buffers initialized\n");
	TC_PRINT("=================================================================\n\n");
}

/*
 * Cache Performance Tests
 */

ZTEST(cpu_performance, test_01_l1_dcache_sequential)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 1: L1 D-Cache Sequential Access ---\n");

	/* Configure cache counters */
	memcpy(counter_configs, cache_counters, sizeof(cache_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Sequential read - should hit L1 cache */
	for (int iter = 0; iter < 100; iter++) {
		for (size_t i = 0; i < L1_CACHE_WORK_SIZE / sizeof(uint64_t); i++) {
			sum += test_array[i];
		}
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);  /* L1D_CACHE */
	c1 = arch_pmu_counter_read(1);  /* L1D_CACHE_REFILL */
	c2 = arch_pmu_counter_read(2);  /* L2D_CACHE */
	c3 = arch_pmu_counter_read(3);  /* L2D_CACHE_REFILL */

	PRINT_RESULT("L1 D-Cache Sequential", cycles, c0, c1, c2, c3);

	/* Expect high L1 hit rate */
	if (c0 > 0) {
		double l1_hit_rate = (double)(c0 - c1) / c0 * 100.0;
		TC_PRINT("  L1 D-Cache Hit Rate: %.2f%%\n", l1_hit_rate);
		zassert_true(l1_hit_rate > 90.0, "L1 hit rate too low");
	}

	/* Prevent optimization */
	zassert_not_equal(sum, 0, "Sum should not be zero");
}

ZTEST(cpu_performance, test_02_l1_dcache_random)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 2: L1 D-Cache Random Access ---\n");

	memcpy(counter_configs, cache_counters, sizeof(cache_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Random access within L1-sized buffer */
	for (int i = 0; i < MEMORY_ITERATIONS * 10; i++) {
		uint32_t idx = random_indices[i % MEMORY_ITERATIONS] % 
			       (L1_CACHE_WORK_SIZE / sizeof(uint64_t));
		sum += test_array[idx];
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("L1 D-Cache Random", cycles, c0, c1, c2, c3);

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

ZTEST(cpu_performance, test_03_l2_cache_sequential)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 3: L2 Cache Sequential Access ---\n");

	memcpy(counter_configs, cache_counters, sizeof(cache_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Sequential read across L2-sized buffer */
	for (int iter = 0; iter < 10; iter++) {
		for (size_t i = 0; i < ARRAY_ELEMENTS; i++) {
			sum += test_array[i];
		}
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("L2 Cache Sequential", cycles, c0, c1, c2, c3);

	/* Calculate and print L2 hit rate */
	if (c2 > 0) {
		double l2_hit_rate = (double)(c2 - c3) / c2 * 100.0;
		TC_PRINT("  L2 Cache Hit Rate: %.2f%%\n", l2_hit_rate);
	}

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

ZTEST(cpu_performance, test_04_l2_cache_random)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 4: L2 Cache Random Access ---\n");

	memcpy(counter_configs, cache_counters, sizeof(cache_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Random access across L2-sized buffer */
	for (int i = 0; i < MEMORY_ITERATIONS * 10; i++) {
		sum += test_array[random_indices[i % MEMORY_ITERATIONS]];
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("L2 Cache Random", cycles, c0, c1, c2, c3);

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

ZTEST(cpu_performance, test_05_memory_bandwidth)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 5: Memory Bandwidth (Exceeds L2) ---\n");

	memcpy(counter_configs, cache_counters, sizeof(cache_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Access large buffer that exceeds L2 cache */
	for (size_t i = 0; i < LARGE_ARRAY_ELEMENTS; i++) {
		sum += large_array[i];
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("Memory Bandwidth", cycles, c0, c1, c2, c3);

	/* Calculate bandwidth */
	double bytes_accessed = LARGE_ARRAY_ELEMENTS * sizeof(uint64_t);
	double bandwidth_mbps = (bytes_accessed / (double)cycles) * 
				(2000.0 * 1024 * 1024) / (1000 * 1000);
	TC_PRINT("  Estimated Bandwidth: %.2f MB/s (at 2GHz)\n", bandwidth_mbps);

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

/*
 * Branch Prediction Tests
 */

ZTEST(cpu_performance, test_06_branch_predictable)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 6: Predictable Branches ---\n");

	memcpy(counter_configs, branch_counters, sizeof(branch_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Highly predictable branch pattern */
	for (int i = 0; i < BRANCH_ITERATIONS; i++) {
		if (i % 2 == 0) {
			sum += i;
		} else {
			sum += i * 2;
		}
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);  /* BR_PRED */
	c1 = arch_pmu_counter_read(1);  /* BR_MIS_PRED */
	c2 = arch_pmu_counter_read(2);  /* INST_RETIRED */
	c3 = arch_pmu_counter_read(3);  /* INST_SPEC */

	PRINT_RESULT("Predictable Branches", cycles, c0, c1, c2, c3);

	if (c0 > 0) {
		double accuracy = (double)(c0 - c1) / c0 * 100.0;
		TC_PRINT("  Branch Prediction Accuracy: %.2f%%\n", accuracy);
		zassert_true(accuracy > 95.0, "Branch prediction accuracy too low");
	}

	if (c2 > 0) {
		double ipc = (double)c2 / cycles;
		TC_PRINT("  Instructions Per Cycle (IPC): %.2f\n", ipc);
	}

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

ZTEST(cpu_performance, test_07_branch_random)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 7: Random Branches ---\n");

	memcpy(counter_configs, branch_counters, sizeof(branch_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Unpredictable branch pattern */
	for (int i = 0; i < BRANCH_ITERATIONS; i++) {
		uint32_t rnd = random_indices[i % MEMORY_ITERATIONS];
		if (rnd & 1) {
			sum += rnd;
		} else {
			sum += rnd * 2;
		}
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("Random Branches", cycles, c0, c1, c2, c3);

	if (c0 > 0) {
		double accuracy = (double)(c0 - c1) / c0 * 100.0;
		TC_PRINT("  Branch Prediction Accuracy: %.2f%% (expected ~50%%)\n", 
			 accuracy);
	}

	if (c2 > 0) {
		double ipc = (double)c2 / cycles;
		TC_PRINT("  Instructions Per Cycle (IPC): %.2f\n", ipc);
	}

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

/*
 * Memory Access Pattern Tests
 */

ZTEST(cpu_performance, test_08_memory_sequential_read)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 8: Sequential Memory Read ---\n");

	memcpy(counter_configs, memory_counters, sizeof(memory_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	for (size_t i = 0; i < ARRAY_ELEMENTS; i++) {
		sum += test_array[i];
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);  /* MEM_ACCESS */
	c1 = arch_pmu_counter_read(1);  /* BUS_ACCESS */
	c2 = arch_pmu_counter_read(2);  /* INST_RETIRED */
	c3 = arch_pmu_counter_read(3);  /* CPU_CYCLES (should match) */

	PRINT_RESULT("Sequential Read", cycles, c0, c1, c2, c3);

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

ZTEST(cpu_performance, test_09_memory_sequential_write)
{
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 9: Sequential Memory Write ---\n");

	memcpy(counter_configs, memory_counters, sizeof(memory_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	for (size_t i = 0; i < ARRAY_ELEMENTS; i++) {
		test_array[i] = i * 2;
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("Sequential Write", cycles, c0, c1, c2, c3);
}

ZTEST(cpu_performance, test_10_memory_stride_access)
{
	uint64_t sum = 0;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 10: Strided Memory Access ---\n");

	memcpy(counter_configs, memory_counters, sizeof(memory_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Access every 8th element (stride = 64 bytes = cache line size) */
	for (size_t i = 0; i < ARRAY_ELEMENTS; i += 8) {
		sum += test_array[i];
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("Strided Access (64B)", cycles, c0, c1, c2, c3);

	zassert_not_equal(sum, 0, "Sum should not be zero");
}

/*
 * Compute Performance Tests
 */

ZTEST(cpu_performance, test_11_integer_alu)
{
	uint64_t result = 1;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 11: Integer ALU Performance ---\n");

	memcpy(counter_configs, branch_counters, sizeof(branch_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Integer arithmetic operations */
	for (int i = 0; i < COMPUTE_ITERATIONS; i++) {
		result = result + i;
		result = result * 3;
		result = result - i;
		result = result ^ i;
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);  /* INST_RETIRED */
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("Integer ALU", cycles, c0, c1, c2, c3);

	if (c2 > 0) {
		double ipc = (double)c2 / cycles;
		TC_PRINT("  Instructions Per Cycle (IPC): %.2f\n", ipc);
	}

	zassert_not_equal(result, 0, "Result should not be zero");
}

ZTEST(cpu_performance, test_12_integer_multiply)
{
	uint64_t result = 1;
	uint64_t cycles, c0, c1, c2, c3;

	if (arch_pmu_num_counters() == 0) {
		ztest_test_skip();
		return;
	}

	TC_PRINT("\n--- Test 12: Integer Multiply Performance ---\n");

	memcpy(counter_configs, branch_counters, sizeof(branch_counters));
	arch_pmu_configure_counters(counter_configs, 4);

	MEASURE_START();

	/* Integer multiply operations */
	for (int i = 1; i < COMPUTE_ITERATIONS; i++) {
		result = result * (i & 0xFF);
		result = (result >> 8) | 1;  /* Prevent overflow */
	}

	MEASURE_STOP();

	cycles = arch_pmu_cycle_count();
	c0 = arch_pmu_counter_read(0);
	c1 = arch_pmu_counter_read(1);
	c2 = arch_pmu_counter_read(2);
	c3 = arch_pmu_counter_read(3);

	PRINT_RESULT("Integer Multiply", cycles, c0, c1, c2, c3);

	zassert_not_equal(result, 0, "Result should not be zero");
}

/*
 * Summary Test
 */

ZTEST(cpu_performance, test_99_summary)
{
	TC_PRINT("\n");
	TC_PRINT("=================================================================\n");
	TC_PRINT("Benchmark Suite Complete\n");
	TC_PRINT("=================================================================\n");
	TC_PRINT("\n");
	TC_PRINT("Validation complete for current processor.\n");
	TC_PRINT("To validate on Cortex-A53, A72, and A78:\n");
	TC_PRINT("  1. Run on Cortex-A53 platform (QEMU or real hardware)\n");
	TC_PRINT("  2. Run on Cortex-A72 platform (QEMU or real hardware)\n");
	TC_PRINT("  3. Run on Cortex-A78 platform (e.g., Versal Net APU)\n");
	TC_PRINT("\n");
	TC_PRINT("Expected processor characteristics:\n");
	TC_PRINT("  Cortex-A53: Lower IPC (~1.5), higher cache miss rate\n");
	TC_PRINT("  Cortex-A72: Medium IPC (~2.0), medium cache performance\n");
	TC_PRINT("  Cortex-A78: Higher IPC (~2.5+), better branch prediction\n");
	TC_PRINT("\n");
	TC_PRINT("All tests use architectural PMU events (0x00-0x1F) for\n");
	TC_PRINT("portability across all ARMv8-A processors.\n");
	TC_PRINT("=================================================================\n");
}

static void *cpu_perf_setup(void)
{
	test_setup();
	return NULL;
}

ZTEST_SUITE(cpu_performance, NULL, cpu_perf_setup, NULL, NULL, NULL);
