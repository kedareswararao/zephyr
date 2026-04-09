/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm64/pmu.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

LOG_MODULE_REGISTER(pmu, CONFIG_ARM64_PMU_LOG_LEVEL);

/*
 * Instruction Synchronization Barrier — required by the ARM PMUv3 architecture
 * after writing PMCR_EL0, PMSELR_EL0, PMXEVTYPER_EL0, and PMXEVCNTR_EL0 to
 * guarantee the effects are visible to subsequent accesses.
 */
#define pmu_isb() __asm__ volatile("isb" ::: "memory")

/*
 * AArch64 system register access for PMU and calibration.
 *
 * Use explicit MRS/MSR ALWAYS_INLINE helpers instead of read_sysreg() /
 * write_sysreg() from lib_helpers.h so static analysis does not flag GNU
 * statement-expression macros (Sonar c:S3715).
 */
static ALWAYS_INLINE uint64_t pmu_read_cntfrq_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE uint64_t pmu_read_cntvct_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntvct_el0" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE uint64_t pmu_read_id_aa64dfr0_el1(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, id_aa64dfr0_el1" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE uint64_t pmu_read_pmcr_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, pmcr_el0" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE void pmu_write_pmcr_el0(uint64_t v)
{
	__asm__ volatile("msr pmcr_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE uint64_t pmu_read_pmccntr_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, pmccntr_el0" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE void pmu_write_pmcntenset_el0(uint64_t v)
{
	__asm__ volatile("msr pmcntenset_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE void pmu_write_pmcntenclr_el0(uint64_t v)
{
	__asm__ volatile("msr pmcntenclr_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE void pmu_write_pmovsclr_el0(uint64_t v)
{
	__asm__ volatile("msr pmovsclr_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE uint64_t pmu_read_pmovsclr_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, pmovsclr_el0" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE void pmu_write_pmuserenr_el0(uint64_t v)
{
	__asm__ volatile("msr pmuserenr_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE void pmu_write_pmselr_el0(uint64_t v)
{
	__asm__ volatile("msr pmselr_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE void pmu_write_pmxevtyper_el0(uint64_t v)
{
	__asm__ volatile("msr pmxevtyper_el0, %0" :: "r"(v) : "memory");
}

static ALWAYS_INLINE uint64_t pmu_read_pmxevcntr_el0(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, pmxevcntr_el0" : "=r"(v) :: "memory");
	return v;
}

static ALWAYS_INLINE void pmu_write_pmxevcntr_el0(uint64_t v)
{
	__asm__ volatile("msr pmxevcntr_el0, %0" :: "r"(v) : "memory");
}

/* PMU state */
static struct {
	uint32_t num_counters;
	uint32_t num_configured;
	uint32_t implementer;
	uint32_t idcode;
	/*
	 * Use atomic_t so that the initialized flag is visible across CPUs
	 * without a lock.  SYS_INIT runs on the boot CPU before secondary
	 * cores start, so only one writer exists at init time; the atomic
	 * ensures subsequent readers on any CPU see the final value.
	 */
	uint32_t cpu_freq_mhz;
	atomic_t initialized;
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

const char *pmu_event_name(uint32_t event)
{
	for (size_t i = 0; event_names[i].name != NULL; i++) {
		if (event_names[i].code == event) {
			return event_names[i].name;
		}
	}
	return "UNKNOWN";
}

/*
 * Calibrate CPU clock frequency by comparing PMCCNTR_EL0 (CPU cycles)
 * against CNTVCT_EL0 (system timer ticks at CNTFRQ_EL0 Hz).
 * We run the cycle counter for a short interval and compute:
 *
 *   CPU_MHz = (pmu_delta * cntfrq) / (cnt_delta * 1,000,000)
 *
 * This provides a runtime measurement that works on any platform,
 * eliminating the need for a static Kconfig frequency setting.
 */
static void pmu_calibrate_cpu_freq(void)
{
	uint64_t cntfrq = pmu_read_cntfrq_el0();
	uint64_t pmcr_save = pmu_read_pmcr_el0();
	uint64_t cnt_start, pmu_start, pmu_end, cnt_end;
	uint64_t wait_ticks, cnt_now;
	uint64_t pmu_delta, cnt_delta;

	/* Enable cycle counter, reset it, start counting */
	pmu_write_pmcr_el0(pmcr_save | PMCR_E | PMCR_C | PMCR_LC);
	pmu_isb();
	pmu_write_pmcntenset_el0(BIT(31));

	cnt_start = pmu_read_cntvct_el0();
	pmu_start = pmu_read_pmccntr_el0();

	/*
	 * Spin for ~1 ms worth of system timer ticks.
	 * cntfrq / 1000 ticks = 1 ms.
	 * Bound the loop to prevent hanging on broken platforms where
	 * the virtual counter never advances.
	 */
	wait_ticks = cntfrq / 1000;

	uint32_t max_spins = 10000000U;

	do {
		cnt_now = pmu_read_cntvct_el0();
		if (--max_spins == 0) {
			LOG_ERR("CPU frequency calibration timed out "
				"(virtual counter stalled)");
			pmu_state.cpu_freq_mhz = 0;
			pmu_write_pmcr_el0(pmcr_save);
			pmu_isb();
			pmu_write_pmcntenclr_el0(BIT(31));
			return;
		}
	} while ((cnt_now - cnt_start) < wait_ticks);

	pmu_end = pmu_read_pmccntr_el0();
	cnt_end = cnt_now;

	/* Restore PMCR (stop counting, clear enable bit) */
	pmu_write_pmcr_el0(pmcr_save);
	pmu_isb();
	pmu_write_pmcntenclr_el0(BIT(31));

	pmu_delta = pmu_end - pmu_start;
	cnt_delta = cnt_end - cnt_start;

	if (cnt_delta > 0 && cntfrq > 0) {
		pmu_state.cpu_freq_mhz =
			(uint32_t)((pmu_delta * cntfrq) /
				   (cnt_delta * 1000000ULL));
		LOG_INF("CPU frequency calibrated: %u MHz "
			"(pmu_delta=%llu, cnt_delta=%llu, cntfrq=%llu)",
			pmu_state.cpu_freq_mhz, pmu_delta,
			cnt_delta, cntfrq);
	} else {
		pmu_state.cpu_freq_mhz = 0;
		LOG_WRN("CPU frequency calibration failed "
			"(cnt_delta=%llu, cntfrq=%llu)",
			cnt_delta, cntfrq);
	}
}

int pmu_init(void)
{
	uint64_t pmcr;
	uint64_t id_aa64dfr0;
	uint8_t pmu_ver;

	if (atomic_get(&pmu_state.initialized)) {
		return 0;
	}

	/* Check if PMU is available via ID_AA64DFR0_EL1 */
	id_aa64dfr0 = pmu_read_id_aa64dfr0_el1();
	pmu_ver = (id_aa64dfr0 >> 8) & 0xF;

	if (pmu_ver == 0 || pmu_ver == 0xF) {
		LOG_ERR("PMU not implemented or unavailable (version=0x%x)", pmu_ver);
		LOG_ERR("This may be due to QEMU limitations or disabled PMU");
		pmu_state.num_counters = 0;
		atomic_set(&pmu_state.initialized, 1);
		return -ENOTSUP;
	}

	LOG_DBG("PMU version: 0x%x (PMUv3)", pmu_ver);

	/* Read PMCR_EL0 to get PMU capabilities */
	pmcr = pmu_read_pmcr_el0();

	/* Extract number of event counters */
	pmu_state.num_counters = (pmcr >> PMCR_N_SHIFT) & PMCR_N_MASK;
	pmu_state.implementer = (pmcr >> PMCR_IMP_SHIFT) & PMCR_IMP_MASK;
	pmu_state.idcode = (pmcr >> PMCR_IDCODE_SHIFT) & PMCR_IDCODE_MASK;

	if (pmu_state.num_counters == 0) {
		LOG_ERR("No PMU event counters available");
		LOG_ERR("PMCR_EL0=0x%llx - PMU may be disabled by hypervisor/EL3", pmcr);
		atomic_set(&pmu_state.initialized, 1);
		return -ENOTSUP;
	}

	/*
	 * Reset all counters and enable the 64-bit cycle counter.
	 * ISB required after PMCR write for reset to take effect (ARM DDI0487).
	 */
	pmu_write_pmcr_el0(PMCR_P | PMCR_C | PMCR_LC);
	pmu_isb();

	/* Clear all overflow flags */
	pmu_write_pmovsclr_el0(0xFFFFFFFFUL);

	/* Disable all counters initially */
	pmu_write_pmcntenclr_el0(0xFFFFFFFFUL);

	/* Enable user-mode access if configured */
#ifdef CONFIG_ARM64_PMU_USER_ACCESS
	pmu_write_pmuserenr_el0(PMUSERENR_EN | PMUSERENR_SW |
				PMUSERENR_CR | PMUSERENR_ER);
#else
	pmu_write_pmuserenr_el0(0x00UL);
#endif

	/* Calibrate CPU clock frequency from PMU cycle counter vs system timer */
	pmu_calibrate_cpu_freq();

	/*
	 * Write initialized last, after all state is committed.  The atomic
	 * store acts as the publication barrier: any CPU that reads
	 * initialized == 1 is guaranteed to see the fully written state.
	 */
	atomic_set(&pmu_state.initialized, 1);

	LOG_INF("PMU initialized: %u counters, implementer=0x%02x, id=0x%02x",
		pmu_state.num_counters, pmu_state.implementer, pmu_state.idcode);

	return 0;
}

uint32_t pmu_num_counters(void)
{
	return pmu_state.num_counters;
}

uint32_t pmu_cpu_freq_mhz(void)
{
	return pmu_state.cpu_freq_mhz;
}

void pmu_get_info(uint32_t *implementer, uint32_t *idcode)
{
	if (implementer) {
		*implementer = pmu_state.implementer;
	}
	if (idcode) {
		*idcode = pmu_state.idcode;
	}
}

int pmu_counter_config(uint32_t counter, uint32_t event)
{
	unsigned int key;

	if (!atomic_get(&pmu_state.initialized)) {
		return -ENODEV;
	}

	if (counter >= pmu_state.num_counters) {
		return -EINVAL;
	}

	/*
	 * The PMSELR → PMXEVTYPER sequence must be atomic w.r.t. interrupts.
	 * An interrupt handler that modifies PMSELR between the select and the
	 * type write would corrupt the configuration.
	 */
	key = arch_irq_lock();
	pmu_write_pmselr_el0(counter);
	pmu_isb();
	pmu_write_pmxevtyper_el0(event);
	pmu_isb();
	arch_irq_unlock(key);

	LOG_DBG("Counter %u configured for event 0x%02x (%s)",
		counter, event, pmu_event_name(event));

	return 0;
}

void pmu_counter_enable(uint32_t counter)
{
	if (counter < pmu_state.num_counters) {
		pmu_write_pmcntenset_el0(BIT(counter));
	}
}

void pmu_counter_disable(uint32_t counter)
{
	if (counter < pmu_state.num_counters) {
		pmu_write_pmcntenclr_el0(BIT(counter));
	}
}

void pmu_counter_enable_all(void)
{
	uint32_t mask = (1U << pmu_state.num_counters) - 1;

	/* Enable all event counters + cycle counter (bit 31) */
	pmu_write_pmcntenset_el0(mask | BIT(31));
}

void pmu_counter_disable_all(void)
{
	pmu_write_pmcntenclr_el0(0xFFFFFFFFUL);
}

uint64_t pmu_counter_read(uint32_t counter)
{
	unsigned int key;
	uint64_t value;

	if (!atomic_get(&pmu_state.initialized) || counter >= pmu_state.num_counters) {
		return 0;
	}

	/*
	 * The PMSELR → PMXEVCNTR read sequence must be atomic w.r.t. interrupts
	 * to prevent an interrupt from changing PMSELR between the select and
	 * the read, which would return the wrong counter's value.
	 */
	key = arch_irq_lock();
	pmu_write_pmselr_el0(counter);
	pmu_isb();
	value = pmu_read_pmxevcntr_el0();
	arch_irq_unlock(key);

	return value;
}

void pmu_counter_reset(uint32_t counter)
{
	unsigned int key;

	if (!atomic_get(&pmu_state.initialized) || counter >= pmu_state.num_counters) {
		return;
	}

	/* Select + write sequence must be atomic w.r.t. interrupts */
	key = arch_irq_lock();
	pmu_write_pmselr_el0(counter);
	pmu_isb();
	pmu_write_pmxevcntr_el0(0UL);
	pmu_isb();
	arch_irq_unlock(key);
}

void pmu_counter_reset_all(void)
{
	unsigned int key;
	uint64_t pmcr;

	/*
	 * Read-modify-write of PMCR_EL0 must be atomic w.r.t. interrupts to
	 * prevent an ISR from changing PMCR between the read and write.
	 */
	key = arch_irq_lock();
	pmcr = pmu_read_pmcr_el0();
	pmu_write_pmcr_el0(pmcr | PMCR_P);
	pmu_isb();
	arch_irq_unlock(key);
}

uint64_t pmu_cycle_count(void)
{
	return pmu_read_pmccntr_el0();
}

void pmu_cycle_reset(void)
{
	unsigned int key;
	uint64_t pmcr;

	key = arch_irq_lock();
	pmcr = pmu_read_pmcr_el0();
	pmu_write_pmcr_el0(pmcr | PMCR_C);
	pmu_isb();
	arch_irq_unlock(key);
}

void pmu_start(void)
{
	unsigned int key;
	uint64_t pmcr;

	key = arch_irq_lock();
	pmcr = pmu_read_pmcr_el0();
	pmu_write_pmcr_el0(pmcr | PMCR_E);
	pmu_isb();
	arch_irq_unlock(key);

	/* Ensure cycle counter is enabled (PMCNTENSET is write-set-only) */
	pmu_write_pmcntenset_el0(BIT(31));
}

void pmu_stop(void)
{
	unsigned int key;
	uint64_t pmcr;

	key = arch_irq_lock();
	pmcr = pmu_read_pmcr_el0();
	pmu_write_pmcr_el0(pmcr & ~PMCR_E);
	pmu_isb();
	arch_irq_unlock(key);
}

bool pmu_counter_overflow(uint32_t counter)
{
	uint64_t ovsr;

	if (!atomic_get(&pmu_state.initialized) || counter >= pmu_state.num_counters) {
		return false;
	}

	ovsr = pmu_read_pmovsclr_el0();
	return (ovsr & BIT(counter)) != 0;
}

void pmu_counter_clear_overflow(uint32_t counter)
{
	if (!atomic_get(&pmu_state.initialized) || counter >= pmu_state.num_counters) {
		return;
	}

	/* Write 1 to clear overflow bit */
	pmu_write_pmovsclr_el0(BIT(counter));
}

int pmu_configure_counters(const struct pmu_counter_config *configs,
				 uint32_t num_configs)
{
	uint32_t i;
	int ret;

	if (!atomic_get(&pmu_state.initialized)) {
		return -ENODEV;
	}

	if (!configs) {
		return -EINVAL;
	}

	if (num_configs > pmu_state.num_counters) {
		LOG_ERR("Requested %u counters, only %u available",
			num_configs, pmu_state.num_counters);
		return -EINVAL;
	}

	/* Disable all counters during configuration */
	pmu_counter_disable_all();

	for (i = 0; i < num_configs; i++) {
		ret = pmu_counter_config(i, configs[i].event);
		if (ret < 0) {
			return ret;
		}

		if (configs[i].enabled) {
			pmu_counter_enable(i);
		}
	}

	pmu_state.num_configured = num_configs;

	return 0;
}

int pmu_measure(void (*func)(void *), void *arg,
		     struct pmu_measurement *result)
{
	uint32_t n;
	uint32_t i;

	if (!atomic_get(&pmu_state.initialized)) {
		return -ENODEV;
	}

	if (!func || !result) {
		return -EINVAL;
	}

	/* Reset all counters */
	pmu_counter_reset_all();
	pmu_cycle_reset();

	/* Start counting */
	pmu_start();

	/* Execute the function */
	func(arg);

	/* Stop counting */
	pmu_stop();

	/* Read results — only read counters that were explicitly configured */
	result->cycles = pmu_cycle_count();
	n = pmu_state.num_configured > 0
	  ? pmu_state.num_configured : pmu_state.num_counters;
	result->num_counters = n;

	for (i = 0; i < n && i < PMU_MAX_COUNTERS; i++) {
		result->counters[i] = pmu_counter_read(i);
	}

	return 0;
}

static int pmu_init_device(void)
{
	/* Ignore return value: PMU absence is expected on some platforms
	 * (e.g., QEMU) and is not a boot failure.
	 */
	(void)pmu_init();
	return 0;
}

SYS_INIT(pmu_init_device, POST_KERNEL, CONFIG_ARM64_PMU_INIT_PRIORITY);
