/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/pmu.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(pmu, CONFIG_ARM64_PMU_LOG_LEVEL);

/* System register access macros */
#define read_sysreg(reg) ({ \
	uint64_t _val; \
	__asm__ volatile("mrs %0, " #reg : "=r"(_val)); \
	_val; \
})

#define write_sysreg(reg, val) \
	__asm__ volatile("msr " #reg ", %0" :: "r"((uint64_t)(val)) : "memory")

#define isb() __asm__ volatile("isb" ::: "memory")

/* PMU state */
static struct {
	uint32_t num_counters;
	uint32_t implementer;
	uint32_t idcode;
	bool initialized;
} pmu_state;

/* Event name lookup table */
struct event_info {
	uint32_t code;
	const char *name;
};

static const struct event_info event_names[] = {
	{PMU_EVT_SW_INCR,           "SW_INCR"},
	{PMU_EVT_L1I_CACHE_REFILL,  "L1I_CACHE_REFILL"},
	{PMU_EVT_L1I_TLB_REFILL,    "L1I_TLB_REFILL"},
	{PMU_EVT_L1D_CACHE_REFILL,  "L1D_CACHE_REFILL"},
	{PMU_EVT_L1D_CACHE,         "L1D_CACHE"},
	{PMU_EVT_L1D_TLB_REFILL,    "L1D_TLB_REFILL"},
	{PMU_EVT_INST_RETIRED,      "INST_RETIRED"},
	{PMU_EVT_EXC_TAKEN,         "EXC_TAKEN"},
	{PMU_EVT_EXC_RETURN,        "EXC_RETURN"},
	{PMU_EVT_BR_MIS_PRED,       "BR_MIS_PRED"},
	{PMU_EVT_CPU_CYCLES,        "CPU_CYCLES"},
	{PMU_EVT_BR_PRED,           "BR_PRED"},
	{PMU_EVT_MEM_ACCESS,        "MEM_ACCESS"},
	{PMU_EVT_L1I_CACHE,         "L1I_CACHE"},
	{PMU_EVT_L1D_CACHE_WB,      "L1D_CACHE_WB"},
	{PMU_EVT_L2D_CACHE,         "L2D_CACHE"},
	{PMU_EVT_L2D_CACHE_REFILL,  "L2D_CACHE_REFILL"},
	{PMU_EVT_L2D_CACHE_WB,      "L2D_CACHE_WB"},
	{PMU_EVT_BUS_ACCESS,        "BUS_ACCESS"},
	{PMU_EVT_MEMORY_ERROR,      "MEMORY_ERROR"},
	{PMU_EVT_INST_SPEC,         "INST_SPEC"},
	{PMU_EVT_TTBR_WRITE,        "TTBR_WRITE"},
	{PMU_EVT_BUS_CYCLES,        "BUS_CYCLES"},
	{0, NULL}
};

const char *arch_pmu_event_name(uint32_t event)
{
	for (int i = 0; event_names[i].name != NULL; i++) {
		if (event_names[i].code == event) {
			return event_names[i].name;
		}
	}
	return "UNKNOWN";
}

int arch_pmu_init(void)
{
	uint64_t pmcr;
	uint64_t id_aa64dfr0;

	if (pmu_state.initialized) {
		return 0;
	}

	/* Check if PMU is available via ID_AA64DFR0_EL1 */
	id_aa64dfr0 = read_sysreg(id_aa64dfr0_el1);
	uint8_t pmu_ver = (id_aa64dfr0 >> 8) & 0xF;
	
	if (pmu_ver == 0 || pmu_ver == 0xF) {
		LOG_WRN("PMU not implemented or unavailable (version=0x%x)", pmu_ver);
		LOG_WRN("This may be due to QEMU limitations or disabled PMU");
		pmu_state.num_counters = 0;
		pmu_state.initialized = true;
		return -ENOTSUP;
	}

	LOG_DBG("PMU version: 0x%x (PMUv3)", pmu_ver);

	/* Read PMCR_EL0 to get PMU capabilities */
	pmcr = read_sysreg(pmcr_el0);

	/* Extract number of event counters */
	pmu_state.num_counters = (pmcr >> PMCR_N_SHIFT) & PMCR_N_MASK;
	pmu_state.implementer = (pmcr >> PMCR_IMP_SHIFT) & PMCR_IMP_MASK;
	pmu_state.idcode = (pmcr >> PMCR_IDCODE_SHIFT) & PMCR_IDCODE_MASK;

	if (pmu_state.num_counters == 0) {
		LOG_WRN("No PMU event counters available");
		LOG_WRN("PMCR_EL0=0x%llx - PMU may be disabled by hypervisor/EL3", pmcr);
		pmu_state.initialized = true;
		return -ENOTSUP;
	}

	/* Reset all counters and enable long cycle counter */
	pmcr = PMCR_P | PMCR_C | PMCR_LC;
	write_sysreg(pmcr_el0, pmcr);

	/* Clear all overflow flags */
	write_sysreg(pmovsclr_el0, 0xFFFFFFFFUL);

	/* Disable all counters initially */
	write_sysreg(pmcntenclr_el0, 0xFFFFFFFFUL);

	/* Enable user-mode access if configured */
#ifdef CONFIG_ARM64_PMU_USER_ACCESS
	write_sysreg(pmuserenr_el0, 0x0F); /* Enable user access */
#else
	write_sysreg(pmuserenr_el0, 0x00); /* Disable user access */
#endif

	pmu_state.initialized = true;

	LOG_INF("PMU initialized: %u counters, implementer=0x%02x, id=0x%02x",
		pmu_state.num_counters, pmu_state.implementer, pmu_state.idcode);

	return 0;
}

uint32_t arch_pmu_num_counters(void)
{
	return pmu_state.num_counters;
}

void arch_pmu_get_info(uint32_t *implementer, uint32_t *idcode)
{
	if (implementer) {
		*implementer = pmu_state.implementer;
	}
	if (idcode) {
		*idcode = pmu_state.idcode;
	}
}

int arch_pmu_counter_config(uint32_t counter, uint32_t event)
{
	if (!pmu_state.initialized) {
		return -ENODEV;
	}

	if (counter >= pmu_state.num_counters) {
		return -EINVAL;
	}

	/* Select the counter */
	write_sysreg(pmselr_el0, counter);
	isb();

	/* Set the event type */
	write_sysreg(pmxevtyper_el0, event);
	isb();

	LOG_DBG("Counter %u configured for event 0x%02x (%s)",
		counter, event, arch_pmu_event_name(event));

	return 0;
}

void arch_pmu_counter_enable(uint32_t counter)
{
	if (counter < pmu_state.num_counters) {
		write_sysreg(pmcntenset_el0, BIT(counter));
	}
}

void arch_pmu_counter_disable(uint32_t counter)
{
	if (counter < pmu_state.num_counters) {
		write_sysreg(pmcntenclr_el0, BIT(counter));
	}
}

void arch_pmu_counter_enable_all(void)
{
	uint32_t mask = (1U << pmu_state.num_counters) - 1;

	/* Enable all event counters + cycle counter (bit 31) */
	write_sysreg(pmcntenset_el0, mask | BIT(31));
}

void arch_pmu_counter_disable_all(void)
{
	write_sysreg(pmcntenclr_el0, 0xFFFFFFFFUL);
}

uint64_t arch_pmu_counter_read(uint32_t counter)
{
	uint64_t value;

	if (!pmu_state.initialized || counter >= pmu_state.num_counters) {
		return 0;
	}

	/* Select the counter */
	write_sysreg(pmselr_el0, counter);
	isb();

	/* Read the counter value */
	value = read_sysreg(pmxevcntr_el0);

	return value;
}

void arch_pmu_counter_reset(uint32_t counter)
{
	if (!pmu_state.initialized || counter >= pmu_state.num_counters) {
		return;
	}

	/* Select the counter */
	write_sysreg(pmselr_el0, counter);
	isb();

	/* Write zero to reset */
	write_sysreg(pmxevcntr_el0, 0);
	isb();
}

void arch_pmu_counter_reset_all(void)
{
	uint64_t pmcr = read_sysreg(pmcr_el0);

	/* Set P bit to reset all event counters */
	write_sysreg(pmcr_el0, pmcr | PMCR_P);
}

uint64_t arch_pmu_cycle_count(void)
{
	return read_sysreg(pmccntr_el0);
}

void arch_pmu_cycle_reset(void)
{
	uint64_t pmcr = read_sysreg(pmcr_el0);

	/* Set C bit to reset cycle counter */
	write_sysreg(pmcr_el0, pmcr | PMCR_C);
}

void arch_pmu_start(void)
{
	uint64_t pmcr = read_sysreg(pmcr_el0);

	/* Set E bit to enable all counters */
	write_sysreg(pmcr_el0, pmcr | PMCR_E);
	isb();

	/* Ensure cycle counter is enabled */
	write_sysreg(pmcntenset_el0, BIT(31));
}

void arch_pmu_stop(void)
{
	uint64_t pmcr = read_sysreg(pmcr_el0);

	/* Clear E bit to disable all counters */
	write_sysreg(pmcr_el0, pmcr & ~PMCR_E);
	isb();
}

bool arch_pmu_counter_overflow(uint32_t counter)
{
	uint64_t ovsr;

	if (!pmu_state.initialized || counter >= pmu_state.num_counters) {
		return false;
	}

	ovsr = read_sysreg(pmovsclr_el0);
	return (ovsr & BIT(counter)) != 0;
}

void arch_pmu_counter_clear_overflow(uint32_t counter)
{
	if (!pmu_state.initialized || counter >= pmu_state.num_counters) {
		return;
	}

	/* Write 1 to clear overflow bit */
	write_sysreg(pmovsclr_el0, BIT(counter));
}

int arch_pmu_configure_counters(const struct pmu_counter_config *configs,
				 uint32_t num_configs)
{
	int ret;

	if (!pmu_state.initialized) {
		return -ENODEV;
	}

	if (num_configs > pmu_state.num_counters) {
		LOG_ERR("Requested %u counters, only %u available",
			num_configs, pmu_state.num_counters);
		return -EINVAL;
	}

	/* Disable all counters during configuration */
	arch_pmu_counter_disable_all();

	for (uint32_t i = 0; i < num_configs; i++) {
		ret = arch_pmu_counter_config(i, configs[i].event);
		if (ret < 0) {
			return ret;
		}

		if (configs[i].enabled) {
			arch_pmu_counter_enable(i);
		}
	}

	return 0;
}

int arch_pmu_measure(void (*func)(void *), void *arg,
		     struct pmu_measurement *result)
{
	if (!pmu_state.initialized || !func || !result) {
		return -EINVAL;
	}

	/* Reset all counters */
	arch_pmu_counter_reset_all();
	arch_pmu_cycle_reset();

	/* Start counting */
	arch_pmu_start();

	/* Execute the function */
	func(arg);

	/* Stop counting */
	arch_pmu_stop();

	/* Read results */
	result->cycles = arch_pmu_cycle_count();
	result->num_counters = pmu_state.num_counters;

	for (uint32_t i = 0; i < pmu_state.num_counters && i < 8; i++) {
		result->counters[i] = arch_pmu_counter_read(i);
	}

	return 0;
}

/* Auto-initialize PMU at boot if configured */
#ifdef CONFIG_ARM64_PMU_INIT_PRIORITY
static int pmu_init_device(void)
{
	return arch_pmu_init();
}

SYS_INIT(pmu_init_device, POST_KERNEL, CONFIG_ARM64_PMU_INIT_PRIORITY);
#endif
