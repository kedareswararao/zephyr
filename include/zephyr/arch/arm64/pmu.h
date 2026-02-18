/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM64 Performance Monitoring Unit (PMU) API
 *
 * This header provides the API for the ARMv8-A PMUv3 driver.
 * The PMU provides hardware performance counters for monitoring
 * CPU behavior including cache misses, branch mispredictions,
 * memory accesses, and cycle counts.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM64_PMU_H_
#define ZEPHYR_INCLUDE_ARCH_ARM64_PMU_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup arm64_pmu ARM64 PMU API
 * @ingroup arch-interface
 * @{
 */

/*
 * PMCR_EL0 Register Bit Definitions
 */
#define PMCR_E          BIT(0)  /* Enable all counters */
#define PMCR_P          BIT(1)  /* Reset all event counters */
#define PMCR_C          BIT(2)  /* Cycle counter reset */
#define PMCR_D          BIT(3)  /* Clock divider (1=64 cycles) */
#define PMCR_X          BIT(4)  /* Export enable */
#define PMCR_DP         BIT(5)  /* Disable cycle counter in EL2 */
#define PMCR_LC         BIT(6)  /* Long cycle counter enable (64-bit) */
#define PMCR_LP         BIT(7)  /* Long event counter enable */

/* PMCR field shifts and masks */
#define PMCR_N_SHIFT    11
#define PMCR_N_MASK     0x1F    /* Number of event counters */
#define PMCR_IDCODE_SHIFT 16
#define PMCR_IDCODE_MASK  0xFF
#define PMCR_IMP_SHIFT  24
#define PMCR_IMP_MASK   0xFF    /* Implementer code */

/*
 * ARMv8-A Architectural PMU Event Codes (0x00-0x1F)
 * These events are guaranteed to be implemented on all ARMv8-A processors.
 */
#define PMU_EVT_SW_INCR           0x00  /* Software increment */
#define PMU_EVT_L1I_CACHE_REFILL  0x01  /* L1 I-cache refill */
#define PMU_EVT_L1I_TLB_REFILL    0x02  /* L1 I-TLB refill */
#define PMU_EVT_L1D_CACHE_REFILL  0x03  /* L1 D-cache refill */
#define PMU_EVT_L1D_CACHE         0x04  /* L1 D-cache access */
#define PMU_EVT_L1D_TLB_REFILL    0x05  /* L1 D-TLB refill */
#define PMU_EVT_INST_RETIRED      0x08  /* Instruction architecturally executed */
#define PMU_EVT_EXC_TAKEN         0x09  /* Exception taken */
#define PMU_EVT_EXC_RETURN        0x0A  /* Exception return executed */
#define PMU_EVT_BR_MIS_PRED       0x10  /* Mispredicted branch */
#define PMU_EVT_CPU_CYCLES        0x11  /* CPU cycles */
#define PMU_EVT_BR_PRED           0x12  /* Predictable branch speculatively executed */
#define PMU_EVT_MEM_ACCESS        0x13  /* Data memory access */
#define PMU_EVT_L1I_CACHE         0x14  /* L1 I-cache access */
#define PMU_EVT_L1D_CACHE_WB      0x15  /* L1 D-cache write-back */
#define PMU_EVT_L2D_CACHE         0x16  /* L2 D-cache access */
#define PMU_EVT_L2D_CACHE_REFILL  0x17  /* L2 D-cache refill */
#define PMU_EVT_L2D_CACHE_WB      0x18  /* L2 D-cache write-back */
#define PMU_EVT_BUS_ACCESS        0x19  /* Bus access */
#define PMU_EVT_MEMORY_ERROR      0x1A  /* Local memory error */
#define PMU_EVT_INST_SPEC         0x1B  /* Instruction speculatively executed */
#define PMU_EVT_TTBR_WRITE        0x1C  /* Write to translation table base */
#define PMU_EVT_BUS_CYCLES        0x1D  /* Bus cycle */

/**
 * @brief PMU counter configuration structure
 */
struct pmu_counter_config {
	/** Event code to monitor (use PMU_EVT_* defines) */
	uint32_t event;
	/** Enable this counter when configured */
	bool enabled;
};

/**
 * @brief PMU measurement result structure
 */
struct pmu_measurement {
	/** Total CPU cycles elapsed */
	uint64_t cycles;
	/** Number of counters with valid data */
	uint32_t num_counters;
	/** Counter values (up to 8 counters) */
	uint64_t counters[8];
};

/**
 * @brief Initialize the PMU driver
 *
 * Detects PMU availability, resets counters, and prepares for use.
 *
 * @return 0 on success, -ENOTSUP if PMU not available, negative errno otherwise
 */
int arch_pmu_init(void);

/**
 * @brief Get the number of available event counters
 *
 * @return Number of event counters (0 if PMU unavailable)
 */
uint32_t arch_pmu_num_counters(void);

/**
 * @brief Get PMU implementer information
 *
 * @param implementer Pointer to store implementer code (or NULL)
 * @param idcode Pointer to store ID code (or NULL)
 */
void arch_pmu_get_info(uint32_t *implementer, uint32_t *idcode);

/**
 * @brief Configure an event counter
 *
 * @param counter Counter index (0 to num_counters-1)
 * @param event Event code (PMU_EVT_* or implementation-defined)
 * @return 0 on success, -EINVAL for invalid counter, -ENODEV if not initialized
 */
int arch_pmu_counter_config(uint32_t counter, uint32_t event);

/**
 * @brief Enable a specific event counter
 *
 * @param counter Counter index
 */
void arch_pmu_counter_enable(uint32_t counter);

/**
 * @brief Disable a specific event counter
 *
 * @param counter Counter index
 */
void arch_pmu_counter_disable(uint32_t counter);

/**
 * @brief Enable all event counters and cycle counter
 */
void arch_pmu_counter_enable_all(void);

/**
 * @brief Disable all event counters
 */
void arch_pmu_counter_disable_all(void);

/**
 * @brief Read an event counter value
 *
 * @param counter Counter index
 * @return Current counter value, or 0 if invalid
 */
uint64_t arch_pmu_counter_read(uint32_t counter);

/**
 * @brief Reset a specific event counter to zero
 *
 * @param counter Counter index
 */
void arch_pmu_counter_reset(uint32_t counter);

/**
 * @brief Reset all event counters to zero
 */
void arch_pmu_counter_reset_all(void);

/**
 * @brief Read the 64-bit cycle counter
 *
 * @return Current cycle count
 */
uint64_t arch_pmu_cycle_count(void);

/**
 * @brief Reset the cycle counter to zero
 */
void arch_pmu_cycle_reset(void);

/**
 * @brief Start all enabled counters
 *
 * Sets PMCR_EL0.E bit to enable counting.
 */
void arch_pmu_start(void);

/**
 * @brief Stop all counters
 *
 * Clears PMCR_EL0.E bit to disable counting.
 */
void arch_pmu_stop(void);

/**
 * @brief Check if a counter has overflowed
 *
 * @param counter Counter index
 * @return true if overflow occurred
 */
bool arch_pmu_counter_overflow(uint32_t counter);

/**
 * @brief Clear overflow flag for a counter
 *
 * @param counter Counter index
 */
void arch_pmu_counter_clear_overflow(uint32_t counter);

/**
 * @brief Configure multiple counters at once
 *
 * @param configs Array of counter configurations
 * @param num_configs Number of configurations
 * @return 0 on success, negative errno on failure
 */
int arch_pmu_configure_counters(const struct pmu_counter_config *configs,
				uint32_t num_configs);

/**
 * @brief Measure a function's execution with all configured counters
 *
 * Resets counters, starts counting, executes the function, stops counting,
 * and stores results.
 *
 * @param func Function to measure
 * @param arg Argument to pass to function
 * @param result Pointer to store measurement results
 * @return 0 on success, -EINVAL if parameters invalid
 */
int arch_pmu_measure(void (*func)(void *), void *arg,
		     struct pmu_measurement *result);

/**
 * @brief Get human-readable name for an event code
 *
 * @param event Event code
 * @return Event name string, or "UNKNOWN" if not recognized
 */
const char *arch_pmu_event_name(uint32_t event);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM64_PMU_H_ */
