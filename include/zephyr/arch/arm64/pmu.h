/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARMv8-A AArch64 PMUv3 — register definitions and aliases
 *
 * Portable applications should include <zephyr/pmu.h> and call @c pmu_*().
 * This header adds AArch64 @c PMCR_EL0 / @c PMUSERENR_EL0 bit definitions
 * and @c arch_pmu_* aliases for existing call sites.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM64_PMU_H_
#define ZEPHYR_INCLUDE_ARCH_ARM64_PMU_H_

#include <zephyr/pmu.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief PMCR_EL0: enable all counters. */
#define PMCR_E          BIT(0)
/** @brief PMCR_EL0: reset all event counters. */
#define PMCR_P          BIT(1)
/** @brief PMCR_EL0: cycle counter reset. */
#define PMCR_C          BIT(2)
/** @brief PMCR_EL0: clock divider (1 = every 64 cycles). */
#define PMCR_D          BIT(3)
/** @brief PMCR_EL0: export enable. */
#define PMCR_X          BIT(4)
/** @brief PMCR_EL0: disable cycle counter in EL2. */
#define PMCR_DP         BIT(5)
/** @brief PMCR_EL0: long cycle counter (64-bit). */
#define PMCR_LC         BIT(6)
/** @brief PMCR_EL0: long event counter enable. */
#define PMCR_LP         BIT(7)

/** @brief PMCR_EL0 bit position for event counter count field. */
#define PMCR_N_SHIFT    11
/** @brief PMCR_EL0 mask for number of implemented event counters. */
#define PMCR_N_MASK     0x1F
/** @brief PMCR_EL0 bit position for ID code field. */
#define PMCR_IDCODE_SHIFT 16
/** @brief PMCR_EL0 mask for ID code field. */
#define PMCR_IDCODE_MASK  0xFF
/** @brief PMCR_EL0 bit position for implementer code field. */
#define PMCR_IMP_SHIFT  24
/** @brief PMCR_EL0 mask for implementer code field. */
#define PMCR_IMP_MASK   0xFF

/** @brief PMUSERENR_EL0: user-mode PMU access enable. */
#define PMUSERENR_EN    BIT(0)
/** @brief PMUSERENR_EL0: software increment at EL0. */
#define PMUSERENR_SW    BIT(1)
/** @brief PMUSERENR_EL0: cycle counter read at EL0. */
#define PMUSERENR_CR    BIT(2)
/** @brief PMUSERENR_EL0: event counter read at EL0. */
#define PMUSERENR_ER    BIT(3)

/** @cond INTERNAL_HIDDEN */
#define arch_pmu_init                   pmu_init
#define arch_pmu_num_counters           pmu_num_counters
#define arch_pmu_cpu_freq_mhz           pmu_cpu_freq_mhz
#define arch_pmu_get_info               pmu_get_info
#define arch_pmu_counter_config         pmu_counter_config
#define arch_pmu_counter_enable         pmu_counter_enable
#define arch_pmu_counter_disable        pmu_counter_disable
#define arch_pmu_counter_enable_all     pmu_counter_enable_all
#define arch_pmu_counter_disable_all    pmu_counter_disable_all
#define arch_pmu_counter_read           pmu_counter_read
#define arch_pmu_counter_reset          pmu_counter_reset
#define arch_pmu_counter_reset_all      pmu_counter_reset_all
#define arch_pmu_cycle_count            pmu_cycle_count
#define arch_pmu_cycle_reset            pmu_cycle_reset
#define arch_pmu_start                  pmu_start
#define arch_pmu_stop                   pmu_stop
#define arch_pmu_counter_overflow       pmu_counter_overflow
#define arch_pmu_counter_clear_overflow pmu_counter_clear_overflow
#define arch_pmu_configure_counters     pmu_configure_counters
#define arch_pmu_measure                pmu_measure
#define arch_pmu_event_name             pmu_event_name
/** @endcond */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM64_PMU_H_ */
