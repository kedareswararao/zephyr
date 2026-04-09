/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Hardware Performance Monitoring Unit (PMU) API
 *
 * Architecture-neutral interface for hardware performance counters.
 * Backends implement these functions for each supported architecture
 * (for example ARMv8-A PMUv3 on AArch64, future ARMv8-R / Cortex-R52,
 * RISC-V HPM CSRs, etc.).
 *
 * Event codes in the @c PMU_EVT_* range @c 0x00-0x1F follow the
 * Arm Architectural PMU encoding for portability across Arm backends
 * that implement PMUv3-class events. Other architectures may map these
 * logical events to implementation-specific counter configuration in
 * their backend driver.
 *
 * @note PMU registers are typically per-CPU. Callers should pin their
 * thread to a specific CPU (e.g. via k_thread_cpu_mask_set()) before
 * configuring or reading counters when SMP is enabled.
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_PMU_H_
#define ZEPHYR_INCLUDE_ZEPHYR_PMU_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup pmu PMU
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Maximum number of logical event counters this API can describe.
 *
 * A given backend may implement fewer active hardware counters.
 */
#define PMU_MAX_COUNTERS 8

/** @brief Architectural software-increment event (0x00). */
#define PMU_EVT_SW_INCR           0x00
/** @brief L1 instruction cache refill (0x01). */
#define PMU_EVT_L1I_CACHE_REFILL  0x01
/** @brief L1 instruction TLB refill (0x02). */
#define PMU_EVT_L1I_TLB_REFILL    0x02
/** @brief L1 data cache refill (0x03). */
#define PMU_EVT_L1D_CACHE_REFILL  0x03
/** @brief L1 data cache access (0x04). */
#define PMU_EVT_L1D_CACHE         0x04
/** @brief L1 data TLB refill (0x05). */
#define PMU_EVT_L1D_TLB_REFILL    0x05
/** @brief Instruction architecturally executed (0x08). */
#define PMU_EVT_INST_RETIRED      0x08
/** @brief Exception taken (0x09). */
#define PMU_EVT_EXC_TAKEN         0x09
/** @brief Exception return executed (0x0A). */
#define PMU_EVT_EXC_RETURN        0x0A
/** @brief Mispredicted branch (0x10). */
#define PMU_EVT_BR_MIS_PRED       0x10
/** @brief CPU cycle count (0x11). */
#define PMU_EVT_CPU_CYCLES        0x11
/** @brief Predictable branch speculatively executed (0x12). */
#define PMU_EVT_BR_PRED           0x12
/** @brief Data memory access (0x13). */
#define PMU_EVT_MEM_ACCESS        0x13
/** @brief L1 instruction cache access (0x14). */
#define PMU_EVT_L1I_CACHE         0x14
/** @brief L1 data cache write-back (0x15). */
#define PMU_EVT_L1D_CACHE_WB      0x15
/** @brief L2 data cache access (0x16). */
#define PMU_EVT_L2D_CACHE         0x16
/** @brief L2 data cache refill (0x17). */
#define PMU_EVT_L2D_CACHE_REFILL  0x17
/** @brief L2 data cache write-back (0x18). */
#define PMU_EVT_L2D_CACHE_WB      0x18
/** @brief Bus access (0x19). */
#define PMU_EVT_BUS_ACCESS        0x19
/** @brief Local memory error (0x1A). */
#define PMU_EVT_MEMORY_ERROR      0x1A
/** @brief Instruction speculatively executed (0x1B). */
#define PMU_EVT_INST_SPEC         0x1B
/** @brief Write to translation table base (0x1C). */
#define PMU_EVT_TTBR_WRITE        0x1C
/** @brief Bus cycle (0x1D). */
#define PMU_EVT_BUS_CYCLES        0x1D

/** PMU counter configuration entry */
struct pmu_counter_config {
	/** Event code (e.g. @c PMU_EVT_* ) */
	uint32_t event;
	/** Enable this counter when configured */
	bool enabled;
};

/** Result of a bounded PMU measurement */
struct pmu_measurement {
	/** CPU cycles elapsed */
	uint64_t cycles;
	/** Valid entries in @a counters */
	uint32_t num_counters;
	/** Event counter values */
	uint64_t counters[PMU_MAX_COUNTERS];
};

/**
 * Initialize the PMU. Idempotent on success.
 *
 * @retval 0 Success
 * @retval -ENOTSUP No PMU or not usable on this platform
 */
int pmu_init(void);

/** @return Number of event counters, or 0 if PMU unavailable */
uint32_t pmu_num_counters(void);

/**
 * @return Calibrated CPU frequency in MHz, or 0 if unknown
 */
uint32_t pmu_cpu_freq_mhz(void);

/**
 * @param implementer Optional: PMU implementer code (backend-defined)
 * @param idcode Optional: PMU part ID code (backend-defined)
 */
void pmu_get_info(uint32_t *implementer, uint32_t *idcode);

/**
 * @brief Map hardware event counter @a counter to architectural event @a event.
 *
 * @param counter Event counter index in the range [0, pmu_num_counters()).
 * @param event Event code (e.g. @c PMU_EVT_*).
 *
 * @return 0 on success, negative errno on failure
 */
int pmu_counter_config(uint32_t counter, uint32_t event);

/**
 * @brief Enable counting on hardware event counter @a counter.
 *
 * @param counter Event counter index [0, pmu_num_counters()).
 */
void pmu_counter_enable(uint32_t counter);

/**
 * @brief Disable counting on hardware event counter @a counter.
 *
 * @param counter Event counter index [0, pmu_num_counters()).
 */
void pmu_counter_disable(uint32_t counter);

/**
 * @brief Enable all implemented event counters (and the cycle counter where applicable).
 */
void pmu_counter_enable_all(void);

/**
 * @brief Disable all event counters (and the cycle counter where applicable).
 */
void pmu_counter_disable_all(void);

/**
 * @brief Read the current value of hardware event counter @a counter.
 *
 * @param counter Event counter index [0, pmu_num_counters()).
 *
 * @return Counter value, or 0 if PMU is unavailable or @a counter is out of range
 */
uint64_t pmu_counter_read(uint32_t counter);

/**
 * @brief Reset hardware event counter @a counter to zero.
 *
 * @param counter Event counter index [0, pmu_num_counters()).
 */
void pmu_counter_reset(uint32_t counter);

/**
 * @brief Reset all hardware event counters to zero (implementation-defined;
 *        may include cycle counter policies).
 */
void pmu_counter_reset_all(void);

/**
 * @brief Read the architectural cycle count register (if implemented).
 *
 * @return Cycle count, or 0 if unavailable
 */
uint64_t pmu_cycle_count(void);

/**
 * @brief Reset the cycle count register to zero (implementation-defined).
 */
void pmu_cycle_reset(void);

/**
 * @brief Start counting: enable the PMU globally (implementation-defined).
 */
void pmu_start(void);

/**
 * @brief Stop counting: disable the PMU globally (implementation-defined).
 */
void pmu_stop(void);

/**
 * @brief Return whether hardware event counter @a counter has overflowed
 *        since the flag was cleared.
 *
 * @param counter Event counter index [0, pmu_num_counters()).
 *
 * @return true if overflow is pending, false otherwise or if PMU is unavailable
 */
bool pmu_counter_overflow(uint32_t counter);

/**
 * @brief Clear the overflow flag for hardware event counter @a counter.
 *
 * @param counter Event counter index [0, pmu_num_counters()).
 */
void pmu_counter_clear_overflow(uint32_t counter);

/**
 * @brief Configure multiple counters from @a configs and enable those marked enabled.
 *
 * Disables all counters first, then applies @ref pmu_counter_config for each index
 * in [0, @a num_configs) and optionally enables per-entry.
 *
 * @param configs Array of counter/event pairs (index i configures counter i).
 * @param num_configs Number of valid entries in @a configs; must not exceed
 *                    @ref pmu_num_counters().
 *
 * @return 0 on success, or negative errno (e.g. -ENODEV, -EINVAL)
 */
int pmu_configure_counters(const struct pmu_counter_config *configs, uint32_t num_configs);

/**
 * @brief Reset counters, start the PMU, call @a func(@a arg), stop, and fill @a result.
 *
 * @param func Code under measurement (must not be NULL)
 * @param arg Optional argument passed to @a func
 * @param result Output structure: cycle count and per-counter values (must not be NULL)
 *
 * @return 0 on success, or negative errno
 */
int pmu_measure(void (*func)(void *), void *arg, struct pmu_measurement *result);

/** Human-readable name for @a event, or "UNKNOWN" */
const char *pmu_event_name(uint32_t event);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ZEPHYR_PMU_H_ */
