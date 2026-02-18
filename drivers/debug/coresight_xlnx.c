/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoreSight driver for AMD Xilinx Versal/VersalNet platforms
 *
 * This driver provides CoreSight trace support for Xilinx Versal and VersalNet
 * platforms with Cortex-A78 processors. It supports:
 *
 * - ETM (Embedded Trace Macrocell) for CPU instruction tracing
 * - TMC/ETR (Trace Memory Controller/Embedded Trace Router) for trace capture
 * - Funnel for trace merging from multiple sources
 *
 * The driver can operate in system register mode (for per-CPU ETM access)
 * or memory-mapped mode (for other CoreSight components).
 *
 * Compatible: "xlnx,coresight-1.0"
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>
#include <zephyr/arch/arm64/lib_helpers.h>
#include <errno.h>

#include "coresight_arm.h"
#include "coresight_etm.h"

LOG_MODULE_REGISTER(coresight_xlnx, CONFIG_CORESIGHT_XLNX_LOG_LEVEL);

/*
 * Xilinx Versal/VersalNet CoreSight Base Addresses
 * APU0 CoreSight addresses from Versal Net TRM
 */
#define XLNX_CORESIGHT_BASE		0xF0B00000UL  /* APU0 ROM GPR base */
#define XLNX_CORESIGHT_SIZE		0x100000UL    /* 1MB APU0 CoreSight region */

/* APU0 CoreSight components - Versal Net */
#define XLNX_APU0_CTI_BASE		0xF0B04000UL  /* APU0 CTI */
#define XLNX_APU0_FUNNEL_BASE		0xF0B08000UL  /* APU0 Funnel (4x64) */
#define XLNX_APU0_ETF_BASE		0xF0B0C000UL  /* APU0 ETF (4K) */

/* APU0 DSU (DynamIQ Shared Unit) components */
#define XLNX_APU0_DSU_CTI_BASE		0xF0B20000UL  /* DSU CTI */
#define XLNX_APU0_DSU_ELA_BASE		0xF0B24000UL  /* DSU ELA */

/* APU0 Core0 (Cortex-A78) components */
#define XLNX_APU0_CORE0_DBG_BASE	0xF0B40000UL  /* A78 Debug */
#define XLNX_APU0_CORE0_CTI_BASE	0xF0B44000UL  /* A78 CTI */
#define XLNX_APU0_CORE0_PMU_BASE	0xF0B48000UL  /* A78 PMU */
#define XLNX_APU0_CORE0_ETM_BASE	0xF0B4C000UL  /* A78 ETM */
#define XLNX_APU0_CORE0_ELA_BASE	0xF0B50000UL  /* A78 ELA */
#define XLNX_APU0_CORE0_AM_BASE		0xF0B54000UL  /* A78 Activity Monitor */

/* Legacy aliases for compatibility */
#define XLNX_ETM0_BASE			XLNX_APU0_CORE0_ETM_BASE
#define XLNX_FUNNEL_BASE		XLNX_APU0_FUNNEL_BASE
#define XLNX_TMC_ETR_BASE		XLNX_APU0_ETF_BASE  /* ETF acts as trace sink */

/* TMC (Trace Memory Controller) register offsets */
#define TMC_RSZ			0x004	/* RAM Size Register */
#define TMC_STS			0x00C	/* Status Register */
#define TMC_RRD			0x010	/* RAM Read Data Register */
#define TMC_RRP			0x014	/* RAM Read Pointer Register */
#define TMC_RWP			0x018	/* RAM Write Pointer Register */
#define TMC_TRG			0x01C	/* Trigger Counter Register */
#define TMC_CTL			0x020	/* Control Register */
#define TMC_RWD			0x024	/* RAM Write Data Register */
#define TMC_MODE		0x028	/* Mode Register */
#define TMC_LBUFLEVEL		0x02C	/* Latched Buffer Fill Level */
#define TMC_CBUFLEVEL		0x030	/* Current Buffer Fill Level */
#define TMC_BUFWM		0x034	/* Buffer Level Water Mark */
#define TMC_RRPHI		0x038	/* RAM Read Pointer High */
#define TMC_RWPHI		0x03C	/* RAM Write Pointer High */
#define TMC_AXICTL		0x110	/* AXI Control Register */
#define TMC_DBALO		0x118	/* Data Buffer Address Low */
#define TMC_DBAHI		0x11C	/* Data Buffer Address High */
#define TMC_FFSR		0x300	/* Formatter and Flush Status */
#define TMC_FFCR		0x304	/* Formatter and Flush Control */
#define TMC_PSCR		0x308	/* Periodic Synchronization Counter */
#define TMC_AUTHSTATUS		0xFB8	/* Authentication Status */

/* TMC Status Register bits */
#define TMC_STS_FULL		BIT(0)
#define TMC_STS_TRIGGERED	BIT(1)
#define TMC_STS_TMCREADY	BIT(2)
#define TMC_STS_FTEMPTY		BIT(3)
#define TMC_STS_EMPTY		BIT(4)
#define TMC_STS_MEMERR		BIT(5)

/* TMC Control Register bits */
#define TMC_CTL_CAPT_EN		BIT(0)

/* TMC Mode Register values */
#define TMC_MODE_CIRCULAR_BUFFER	0x0
#define TMC_MODE_SW_FIFO		0x1
#define TMC_MODE_HW_FIFO		0x2

/* TMC FFCR bits */
#define TMC_FFCR_FLUSHMAN	BIT(6)
#define TMC_FFCR_STOP_ON_FLUSH	BIT(12)
#define TMC_FFCR_EN_FMT		BIT(0)
#define TMC_FFCR_EN_TI		BIT(1)

/* TMC device type (from DEVID register) */
#define TMC_DEVID		0xFC8
#define TMC_DEVID_CONFIGTYPE_MASK	0xC0
#define TMC_DEVID_CONFIGTYPE_ETB	0x00	/* Embedded Trace Buffer */
#define TMC_DEVID_CONFIGTYPE_ETR	0x40	/* Embedded Trace Router */
#define TMC_DEVID_CONFIGTYPE_ETF	0x80	/* Embedded Trace FIFO */

/* TMC AXICTL bits */
#define TMC_AXICTL_PROT_CTL_B0	BIT(0)
#define TMC_AXICTL_PROT_CTL_B1	BIT(1)
#define TMC_AXICTL_CACHE_CTL_B0	BIT(2)
#define TMC_AXICTL_CACHE_CTL_B1	BIT(3)
#define TMC_AXICTL_CACHE_CTL_B2	BIT(4)
#define TMC_AXICTL_CACHE_CTL_B3	BIT(5)
#define TMC_AXICTL_WR_BURST_LEN	(0xF << 8)

/* Funnel register offsets */
#define FUNNEL_CTRL		0x000	/* Funnel Control Register */
#define FUNNEL_PRIORITY		0x004	/* Priority Control Register */

/* Funnel Control Register bits */
#define FUNNEL_CTRL_HT_MASK	(0xF << 8)	/* Hold time */
#define FUNNEL_ENS(n)		BIT(n)		/* Enable slave port n */

/* Driver state */
struct coresight_xlnx_data {
	mem_addr_t base;		/* CoreSight base address */
	mem_addr_t etm_base;		/* ETM base address (this CPU) */
	mem_addr_t tmc_base;		/* TMC/ETR/ETF base address */
	mem_addr_t funnel_base;		/* Funnel base address */
	uint8_t *trace_buffer;		/* Trace buffer pointer */
	size_t trace_buffer_size;	/* Trace buffer size in bytes */
	size_t trace_data_size;		/* Actual trace data captured */
	uint32_t etm_trace_id;		/* ETM trace ID */
	bool initialized;		/* Driver initialized flag */
	bool tracing_enabled;		/* Tracing active flag */
	bool trace_supported;		/* CPU supports self-hosted trace */
	bool is_etf;			/* True if ETF, false if ETR */
	struct etm_caps caps;		/* ETM capabilities */
	struct etm_config config;	/* ETM configuration */
};

/* Global driver data */
static struct coresight_xlnx_data cs_data;

/* Default trace buffer (4KB) */
static uint8_t __aligned(4096) default_trace_buffer[CONFIG_CORESIGHT_XLNX_BUFFER_SIZE];

/*
 * Barrier macros for ARM64
 */
#define cs_isb() __asm__ volatile("isb" ::: "memory")
#define cs_dsb(opt) __asm__ volatile("dsb " #opt ::: "memory")

/*
 * CoreSight Lock/Unlock using common definitions
 */
static inline void cs_unlock(mem_addr_t base)
{
	sys_write32(CORESIGHT_UNLOCK_KEY, base + CORESIGHT_LAR_OFFSET);
}

static inline void cs_lock(mem_addr_t base)
{
	sys_write32(0, base + CORESIGHT_LAR_OFFSET);
}

/*
 * TMC (Trace Memory Controller) Functions
 */

static int tmc_wait_for_ready(mem_addr_t tmc_base)
{
	int timeout = 1000;

	while (timeout--) {
		uint32_t sts = sys_read32(tmc_base + TMC_STS);
		if (sts & TMC_STS_TMCREADY) {
			return 0;
		}
		k_busy_wait(10);
	}

	LOG_ERR("TMC not ready timeout");
	return -ETIMEDOUT;
}

static int tmc_flush_and_stop(mem_addr_t tmc_base)
{
	uint32_t ffcr;
	int timeout = 1000;

	cs_unlock(tmc_base);

	/* Trigger flush */
	ffcr = sys_read32(tmc_base + TMC_FFCR);
	ffcr |= TMC_FFCR_STOP_ON_FLUSH | TMC_FFCR_FLUSHMAN;
	sys_write32(ffcr, tmc_base + TMC_FFCR);

	/* Wait for flush complete */
	while (timeout--) {
		ffcr = sys_read32(tmc_base + TMC_FFCR);
		if (!(ffcr & TMC_FFCR_FLUSHMAN)) {
			break;
		}
		k_busy_wait(10);
	}

	/* Wait for TMC ready */
	tmc_wait_for_ready(tmc_base);

	cs_lock(tmc_base);

	return (timeout > 0) ? 0 : -ETIMEDOUT;
}

static int tmc_init(mem_addr_t tmc_base, uintptr_t buf_addr, size_t buf_size)
{
	uint32_t devid;
	uint8_t config_type;

	cs_unlock(tmc_base);

	/* Disable capture before configuration */
	sys_write32(0, tmc_base + TMC_CTL);
	tmc_wait_for_ready(tmc_base);

	/* Detect TMC type from DEVID register */
	devid = sys_read32(tmc_base + TMC_DEVID);
	config_type = (devid & TMC_DEVID_CONFIGTYPE_MASK);

	if (config_type == TMC_DEVID_CONFIGTYPE_ETF ||
	    config_type == TMC_DEVID_CONFIGTYPE_ETB) {
		/* ETF/ETB mode - uses internal SRAM */
		cs_data.is_etf = true;

		/* For ETF: Set to HW FIFO mode for pass-through or
		 * circular buffer mode for local capture */
		sys_write32(TMC_MODE_CIRCULAR_BUFFER, tmc_base + TMC_MODE);

		/* Configure formatter - enable formatting */
		sys_write32(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI, tmc_base + TMC_FFCR);

		LOG_INF("TMC/ETF initialized (internal 4KB FIFO)");
	} else {
		/* ETR mode - uses external DRAM buffer */
		uint32_t axictl;

		cs_data.is_etf = false;

		/* Set RAM size (in 32-bit words) */
		sys_write32(buf_size / 4, tmc_base + TMC_RSZ);

		/* Set buffer address */
		sys_write32((uint32_t)(buf_addr & 0xFFFFFFFF), tmc_base + TMC_DBALO);
		sys_write32((uint32_t)(buf_addr >> 32), tmc_base + TMC_DBAHI);

		/* Reset read/write pointers */
		sys_write32((uint32_t)(buf_addr & 0xFFFFFFFF), tmc_base + TMC_RWP);
		sys_write32((uint32_t)(buf_addr >> 32), tmc_base + TMC_RWPHI);

		/* Configure AXI settings */
		axictl = sys_read32(tmc_base + TMC_AXICTL);
		axictl &= ~TMC_AXICTL_WR_BURST_LEN;
		axictl |= (0xF << 8);  /* Max burst length */
		axictl |= TMC_AXICTL_PROT_CTL_B1;  /* Secure access */
		sys_write32(axictl, tmc_base + TMC_AXICTL);

		/* Set circular buffer mode */
		sys_write32(TMC_MODE_CIRCULAR_BUFFER, tmc_base + TMC_MODE);

		/* Configure formatter */
		sys_write32(TMC_FFCR_EN_FMT | TMC_FFCR_EN_TI, tmc_base + TMC_FFCR);

		LOG_INF("TMC/ETR initialized: buf=0x%lx size=%zu", buf_addr, buf_size);
	}

	cs_lock(tmc_base);

	return 0;
}

static int tmc_enable(mem_addr_t tmc_base)
{
	cs_unlock(tmc_base);

	/* Enable trace capture */
	sys_write32(TMC_CTL_CAPT_EN, tmc_base + TMC_CTL);

	cs_lock(tmc_base);

	LOG_DBG("TMC capture enabled");

	return 0;
}

/**
 * @brief Wait for trace synchronization
 *
 * After enabling ETM, wait for the initial A-sync packet to appear
 * in the trace buffer. This ensures proper decoder synchronization.
 * The ETM generates A-sync (12 bytes of 0x00 followed by 0x80) periodically
 * based on TRCSYNCPR configuration.
 *
 * @param tmc_base TMC base address
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, -ETIMEDOUT if no sync within timeout
 */
static int tmc_wait_for_sync(mem_addr_t tmc_base, uint32_t timeout_ms)
{
	uint32_t buflevel;
	int timeout = timeout_ms;

	cs_unlock(tmc_base);

	/*
	 * Wait for trace buffer to contain at least 16 bytes
	 * (enough for A-sync packet: 12 bytes of 0x00 + 0x80)
	 */
	while (timeout > 0) {
		buflevel = sys_read32(tmc_base + TMC_LBUFLEVEL);

		if (buflevel >= 4) {  /* 4 words = 16 bytes */
			cs_lock(tmc_base);
			LOG_DBG("Trace sync detected: buffer level=%u words", buflevel);
			return 0;
		}

		k_busy_wait(1000);  /* 1ms */
		timeout--;
	}

	cs_lock(tmc_base);
	LOG_WRN("Trace sync timeout - ETM may not be generating trace");
	return -ETIMEDOUT;
}

/* Drain data from ETF internal FIFO into our buffer */
static size_t tmc_etf_drain(mem_addr_t tmc_base, uint8_t *buf, size_t buf_size)
{
	uint32_t sts, buflevel;
	size_t bytes_to_read, bytes_read = 0;
	uint32_t *buf32 = (uint32_t *)buf;

	cs_unlock(tmc_base);

	/* Check if there's any data */
	sts = sys_read32(tmc_base + TMC_STS);
	if (sts & TMC_STS_EMPTY) {
		cs_lock(tmc_base);
		LOG_WRN("ETF is empty, no trace data");
		return 0;
	}

	/* Get current buffer fill level (in 32-bit words) */
	buflevel = sys_read32(tmc_base + TMC_LBUFLEVEL);
	bytes_to_read = buflevel * 4;  /* Convert words to bytes */

	if (bytes_to_read > buf_size) {
		bytes_to_read = buf_size;
	}

	LOG_INF("ETF draining %zu bytes (buflevel=%u words)", bytes_to_read, buflevel);

	/* Read data via RRD register */
	while (bytes_read < bytes_to_read) {
		uint32_t data = sys_read32(tmc_base + TMC_RRD);

		/* Check for special "no more data" marker */
		if (data == 0xFFFFFFFF) {
			sts = sys_read32(tmc_base + TMC_STS);
			if (sts & TMC_STS_EMPTY) {
				break;
			}
		}

		*buf32++ = data;
		bytes_read += 4;
	}

	cs_lock(tmc_base);

	LOG_INF("ETF drained %zu bytes of trace data", bytes_read);

	return bytes_read;
}

static int tmc_disable(mem_addr_t tmc_base)
{
	int ret;

	ret = tmc_flush_and_stop(tmc_base);

	/* If ETF mode, drain data to our buffer before disabling */
	if (cs_data.is_etf) {
		cs_data.trace_data_size = tmc_etf_drain(tmc_base,
							cs_data.trace_buffer,
							cs_data.trace_buffer_size);
	}

	cs_unlock(tmc_base);

	/* Disable capture */
	sys_write32(0, tmc_base + TMC_CTL);

	cs_lock(tmc_base);

	LOG_DBG("TMC capture disabled");

	return ret;
}

/*
 * Funnel Functions
 */

static void funnel_init(mem_addr_t funnel_base, uint32_t ports_mask)
{
	uint32_t ctrl;

	cs_unlock(funnel_base);

	/* Enable specified ports with default hold time */
	ctrl = (0x4 << 8) | (ports_mask & 0xFF);  /* Hold time = 4 */
	sys_write32(ctrl, funnel_base + FUNNEL_CTRL);

	cs_lock(funnel_base);

	LOG_INF("Funnel initialized: ports=0x%02x", ports_mask);
}

/*
 * ETM Functions (using MMIO access like Linux CoreSight driver)
 * Note: ETMv4 is configured via memory-mapped registers, NOT system registers
 */

/* ETM MMIO register access helpers */
static inline uint32_t etm_read(mem_addr_t etm_base, uint32_t reg)
{
	return sys_read32(etm_base + reg);
}

static inline void etm_write(mem_addr_t etm_base, uint32_t reg, uint32_t val)
{
	sys_write32(val, etm_base + reg);
}

/* Wait for ETM to become idle (TRCSTATR.IDLE = 1) */
static int etm_wait_idle(mem_addr_t etm_base)
{
	int timeout = 1000;

	while (timeout--) {
		uint32_t statr = etm_read(etm_base, TRCSTATR);
		if (statr & TRCSTATR_IDLE) {
			return 0;
		}
		k_busy_wait(10);
	}

	LOG_ERR("ETM not idle timeout");
	return -ETIMEDOUT;
}

/* Read ETM ID registers to detect capabilities via MMIO */
static int etm_detect_caps(struct etm_caps *caps)
{
	mem_addr_t etm_base = cs_data.etm_base;
	uint32_t trcidr0, trcidr1, trcidr2, trcidr3, trcidr4, trcidr5;
	uint8_t arch_major, arch_minor;

	/* Unlock ETM for access */
	cs_unlock(etm_base);

	/* Read ETM ID registers */
	trcidr0 = etm_read(etm_base, TRCIDR0);
	trcidr1 = etm_read(etm_base, TRCIDR1);
	trcidr2 = etm_read(etm_base, TRCIDR2);
	trcidr3 = etm_read(etm_base, TRCIDR3);
	trcidr4 = etm_read(etm_base, TRCIDR4);
	trcidr5 = etm_read(etm_base, TRCIDR5);

	/* Check if ETM is present - TRCIDR1 should have valid architecture version */
	arch_major = (trcidr1 >> 8) & 0xF;
	arch_minor = (trcidr1 >> 4) & 0xF;

	if (arch_major == 0 && arch_minor == 0) {
		LOG_WRN("ETM not detected at 0x%lx (TRCIDR1=0x%08x)", etm_base, trcidr1);
		cs_lock(etm_base);
		return -ENODEV;
	}

	LOG_INF("ETM detected: arch=%d.%d TRCIDR0=0x%08x TRCIDR1=0x%08x",
		arch_major, arch_minor, trcidr0, trcidr1);

	/* Parse capabilities from ID registers */
	caps->arch = (arch_major << 4) | arch_minor;

	/* TRCIDR4: number of address comparators, counters, resources */
	caps->nr_addr_cmp = ((trcidr4 >> 0) & 0xF) * 2;  /* pairs */
	caps->nr_cntr = (trcidr4 >> 28) & 0xF;
	caps->nr_resource = ((trcidr4 >> 16) & 0xF) + 1;

	/* TRCIDR0: feature support */
	caps->trcbb = (trcidr0 & TRCIDR0_TRCBB) != 0;
	caps->trccond = (trcidr0 & TRCIDR0_TRCCOND) != 0;
	caps->trccci = (trcidr0 & TRCIDR0_TRCCCI) != 0;
	caps->retstack = (trcidr0 & TRCIDR0_RETSTACK) != 0;

	/* TRCIDR5: trace ID width */
	caps->stallctl = ((trcidr5 >> 27) & 0x1) != 0;

	LOG_INF("ETM caps: addr_cmp=%d cntr=%d retstack=%d",
		caps->nr_addr_cmp, caps->nr_cntr, caps->retstack);

	cs_lock(etm_base);
	return 0;
}

/* Configure ETM for basic instruction tracing via MMIO */
static int etm_configure(struct etm_config *config)
{
	/* Set default configuration */
	config->mode = 0;
	config->cfg = TRCCONFIGR_RS;  /* Enable return stack */
	config->eventctrl0 = 0;
	config->eventctrl1 = 0;
	config->stall_ctrl = 0;
	config->ts_ctrl = 0;
	config->syncfreq = 0x8;  /* Sync every 256 bytes - good for small captures */
	config->ccctlr = 0;
	config->bb_ctrl = 0;
	config->vinst_ctrl = 0x201;  /* SSSTATUS=1, trace all */
	config->viiectlr = 0;
	config->vissctlr = 0;
	config->trace_id = CONFIG_CORESIGHT_XLNX_TRACE_ID;

	LOG_DBG("ETM configured: trace_id=%d", config->trace_id);

	return 0;
}

/* Enable ETM tracing via MMIO (TRCPRGCTLR) */
static int etm_enable(void)
{
	mem_addr_t etm_base = cs_data.etm_base;
	int ret;

	/* Skip if trace not supported */
	if (!cs_data.trace_supported) {
		LOG_DBG("ETM tracing skipped - not supported");
		return 0;
	}

	cs_unlock(etm_base);

	/* Clear OS lock if set */
	etm_write(etm_base, TRCOSLAR, 0);

	/* Wait for ETM to be idle before programming */
	ret = etm_wait_idle(etm_base);
	if (ret < 0) {
		cs_lock(etm_base);
		return ret;
	}

	/* Configure ETM registers */
	etm_write(etm_base, TRCCONFIGR, cs_data.config.cfg);
	etm_write(etm_base, TRCTRACEIDR, cs_data.config.trace_id);
	etm_write(etm_base, TRCSYNCPR, cs_data.config.syncfreq);
	etm_write(etm_base, TRCVICTLR, cs_data.config.vinst_ctrl);
	etm_write(etm_base, TRCEVENTCTL0R, 0);
	etm_write(etm_base, TRCEVENTCTL1R, 0);
	etm_write(etm_base, TRCSTALLCTLR, 0);
	etm_write(etm_base, TRCTSCTLR, 0);
	etm_write(etm_base, TRCCCCTLR, 0);
	etm_write(etm_base, TRCBBCTLR, 0);
	etm_write(etm_base, TRCVIIECTLR, 0);
	etm_write(etm_base, TRCVISSCTLR, 0);

	/* Enable ETM - set EN bit in TRCPRGCTLR */
	etm_write(etm_base, TRCPRGCTLR, TRCPRGCTLR_EN);

	cs_lock(etm_base);

	LOG_INF("ETM tracing enabled via MMIO");

	return 0;
}

/* Disable ETM tracing via MMIO */
static int etm_disable(void)
{
	mem_addr_t etm_base = cs_data.etm_base;

	/* Skip if trace not supported */
	if (!cs_data.trace_supported) {
		return 0;
	}

	cs_unlock(etm_base);

	/* Disable ETM - clear EN bit in TRCPRGCTLR */
	etm_write(etm_base, TRCPRGCTLR, 0);

	/* Wait for ETM to become idle */
	etm_wait_idle(etm_base);

	cs_lock(etm_base);

	LOG_DBG("ETM tracing disabled");

	return 0;
}

/**
 * @brief Print ETM register values for OpenCSD snapshot
 *
 * Prints the ETM configuration in OpenCSD device.ini format
 * for offline trace decoding.
 */
void coresight_xlnx_dump_etm_config(void)
{
	mem_addr_t etm_base = cs_data.etm_base;
	uint32_t trcconfigr, trctraceidr, trcauthstatus;
	uint32_t trcidr0, trcidr1, trcidr2, trcidr3, trcidr4, trcidr5;
	uint32_t trcidr8, trcidr9, trcidr10, trcidr11, trcidr12, trcidr13;

	cs_unlock(etm_base);

	/* Read configuration registers */
	trcconfigr = etm_read(etm_base, TRCCONFIGR);
	trctraceidr = etm_read(etm_base, TRCTRACEIDR);
	trcauthstatus = etm_read(etm_base, TRCAUTHSTATUS);

	/* Read ID registers - TRCIDR0-5 are critical for decoder */
	trcidr0 = etm_read(etm_base, TRCIDR0);
	trcidr1 = etm_read(etm_base, TRCIDR1);
	trcidr2 = etm_read(etm_base, TRCIDR2);
	trcidr3 = etm_read(etm_base, TRCIDR3);
	trcidr4 = etm_read(etm_base, TRCIDR4);
	trcidr5 = etm_read(etm_base, TRCIDR5);
	trcidr8 = etm_read(etm_base, TRCIDR8);
	trcidr9 = etm_read(etm_base, TRCIDR9);
	trcidr10 = etm_read(etm_base, TRCIDR10);
	trcidr11 = etm_read(etm_base, TRCIDR11);
	trcidr12 = etm_read(etm_base, TRCIDR12);
	trcidr13 = etm_read(etm_base, TRCIDR13);

	cs_lock(etm_base);

	/*
	 * Print ETM configuration in OpenCSD snapshot format.
	 * OpenCSD uses word offsets (byte_offset / 4) in the device.ini file.
	 * Ref: https://github.com/Linaro/OpenCSD/blob/master/decoder/tests/snapshots/
	 */
	printk("\n=== OpenCSD ETM Device Configuration (device_0.ini) ===\n");
	printk("[device]\n");
	printk("name=ETM_0\n");
	printk("class=trace_source\n");
	printk("type=ETM4\n\n");
	printk("[regs]\n");
	/* Word offset = byte offset / 4 */
	printk("TRCCONFIGR(0x004)=0x%08X\n", trcconfigr);      /* 0x010 / 4 = 0x004 */
	printk("TRCTRACEIDR(0x010)=0x%08X\n", trctraceidr);    /* 0x040 / 4 = 0x010 */
	printk("TRCAUTHSTATUS(0x3EE)=0x%08X\n", trcauthstatus);/* 0xFB8 / 4 = 0x3EE */
	printk("TRCIDR0(0x078)=0x%08X\n", trcidr0);            /* 0x1E0 / 4 = 0x078 */
	printk("TRCIDR1(0x079)=0x%08X\n", trcidr1);            /* 0x1E4 / 4 = 0x079 */
	printk("TRCIDR2(0x07A)=0x%08X\n", trcidr2);            /* 0x1E8 / 4 = 0x07A */
	printk("TRCIDR3(0x07B)=0x%08X\n", trcidr3);            /* 0x1EC / 4 = 0x07B */
	printk("TRCIDR4(0x07C)=0x%08X\n", trcidr4);            /* 0x1F0 / 4 = 0x07C */
	printk("TRCIDR5(0x07D)=0x%08X\n", trcidr5);            /* 0x1F4 / 4 = 0x07D */
	printk("TRCIDR8(0x060)=0x%08X\n", trcidr8);            /* 0x180 / 4 = 0x060 */
	printk("TRCIDR9(0x061)=0x%08X\n", trcidr9);            /* 0x184 / 4 = 0x061 */
	printk("TRCIDR10(0x062)=0x%08X\n", trcidr10);          /* 0x188 / 4 = 0x062 */
	printk("TRCIDR11(0x063)=0x%08X\n", trcidr11);          /* 0x18C / 4 = 0x063 */
	printk("TRCIDR12(0x064)=0x%08X\n", trcidr12);          /* 0x190 / 4 = 0x064 */
	printk("TRCIDR13(0x065)=0x%08X\n", trcidr13);          /* 0x194 / 4 = 0x065 */
	printk("=== End OpenCSD ETM Configuration ===\n\n");
}

/*
 * Public API
 */

/**
 * @brief Initialize CoreSight tracing
 *
 * @return 0 on success, negative errno on failure
 */
int coresight_xlnx_init(void)
{
	int ret;

	if (cs_data.initialized) {
		return 0;
	}

	/* Set base addresses */
	cs_data.base = XLNX_CORESIGHT_BASE;
	cs_data.etm_base = XLNX_ETM0_BASE;  /* CPU0 ETM */
	cs_data.tmc_base = XLNX_TMC_ETR_BASE;
	cs_data.funnel_base = XLNX_FUNNEL_BASE;

	/* Use default trace buffer */
	cs_data.trace_buffer = default_trace_buffer;
	cs_data.trace_buffer_size = sizeof(default_trace_buffer);

	/* Detect ETM capabilities - sets trace_supported flag */
	ret = etm_detect_caps(&cs_data.caps);
	if (ret < 0) {
		LOG_WRN("ETM detection failed, self-hosted trace disabled");
		cs_data.trace_supported = false;
		/* Continue anyway - TMC can still capture external trace */
	} else {
		cs_data.trace_supported = true;
		LOG_INF("Self-hosted trace supported");
	}

	/* Configure ETM */
	ret = etm_configure(&cs_data.config);
	if (ret < 0) {
		LOG_ERR("ETM configuration failed: %d", ret);
		return ret;
	}

	/* Initialize funnel - enable CPU0 trace port */
	funnel_init(cs_data.funnel_base, FUNNEL_ENS(0));

	/* Initialize TMC (ETF or ETR) */
	ret = tmc_init(cs_data.tmc_base,
			   (uintptr_t)cs_data.trace_buffer,
			   cs_data.trace_buffer_size);
	if (ret < 0) {
		LOG_ERR("TMC initialization failed: %d", ret);
		return ret;
	}

	cs_data.initialized = true;

	LOG_INF("CoreSight Xilinx driver initialized");

	return 0;
}

/**
 * @brief Start CoreSight tracing
 *
 * @return 0 on success, negative errno on failure
 */
int coresight_xlnx_start(void)
{
	int ret;

	if (!cs_data.initialized) {
		return -ENODEV;
	}

	if (cs_data.tracing_enabled) {
		return 0;
	}

	/* Enable TMC capture first */
	ret = tmc_enable(cs_data.tmc_base);
	if (ret < 0) {
		return ret;
	}

	/* Enable ETM tracing */
	ret = etm_enable();
	if (ret < 0) {
		tmc_disable(cs_data.tmc_base);
		return ret;
	}

	/*
	 * Wait for trace synchronization.
	 * The ETM generates periodic A-sync packets that decoders use
	 * to synchronize to the trace stream. Wait for initial sync
	 * to ensure captured trace is decodable.
	 */
	ret = tmc_wait_for_sync(cs_data.tmc_base, 100);  /* 100ms timeout */
	if (ret < 0) {
		LOG_WRN("Trace sync timeout, continuing anyway");
		/* Don't fail - trace may still be usable */
	}

	cs_data.tracing_enabled = true;

	LOG_INF("CoreSight tracing started");

	return 0;
}

/**
 * @brief Stop CoreSight tracing
 *
 * @return 0 on success, negative errno on failure
 */
int coresight_xlnx_stop(void)
{
	if (!cs_data.initialized || !cs_data.tracing_enabled) {
		return -ENODEV;
	}

	/* Disable ETM first */
	etm_disable();

	/* Flush and disable TMC */
	tmc_disable(cs_data.tmc_base);

	cs_data.tracing_enabled = false;

	LOG_INF("CoreSight tracing stopped");

	return 0;
}

/**
 * @brief Get trace buffer information
 *
 * @param buffer Pointer to store buffer address
 * @param size Pointer to store buffer size
 * @return 0 on success, negative errno on failure
 */
int coresight_xlnx_get_trace_buffer(uint8_t **buffer, size_t *size)
{
	if (!cs_data.initialized) {
		return -ENODEV;
	}

	if (buffer) {
		*buffer = cs_data.trace_buffer;
	}
	if (size) {
		*size = cs_data.trace_buffer_size;
	}

	return 0;
}

/**
 * @brief Get current trace data size
 *
 * @return Bytes of trace data captured, or negative errno on failure
 */
int coresight_xlnx_get_trace_offset(void)
{
	if (!cs_data.initialized) {
		return -ENODEV;
	}

	/* For ETF mode, return the drained data size */
	if (cs_data.is_etf) {
		return (int)cs_data.trace_data_size;
	}

	/* For ETR mode, calculate from write pointer */
	cs_unlock(cs_data.tmc_base);

	uint32_t rwp_lo = sys_read32(cs_data.tmc_base + TMC_RWP);
	uint32_t rwp_hi = sys_read32(cs_data.tmc_base + TMC_RWPHI);
	uint64_t rwp = ((uint64_t)rwp_hi << 32) | rwp_lo;
	uint64_t base = (uintptr_t)cs_data.trace_buffer;

	cs_lock(cs_data.tmc_base);

	return (int)(rwp - base);
}

/**
 * @brief Check if trace buffer has wrapped (overflow)
 *
 * @return true if wrapped, false otherwise
 */
bool coresight_xlnx_trace_wrapped(void)
{
	uint32_t sts;

	if (!cs_data.initialized) {
		return false;
	}

	cs_unlock(cs_data.tmc_base);
	sts = sys_read32(cs_data.tmc_base + TMC_STS);
	cs_lock(cs_data.tmc_base);

	return (sts & TMC_STS_FULL) != 0;
}

/*
 * Device initialization
 */

static int coresight_xlnx_device_init(const struct device *dev)
{
	ARG_UNUSED(dev);

#ifdef CONFIG_CORESIGHT_XLNX_AUTO_INIT
	return coresight_xlnx_init();
#else
	return 0;
#endif
}

/* Note: Device instantiation would normally use DT_INST macros for
 * proper devicetree integration. This is a simplified version.
 */
#ifdef CONFIG_CORESIGHT_XLNX
DEVICE_DEFINE(coresight_xlnx, "coresight",
	      coresight_xlnx_device_init, NULL,
	      NULL, NULL,
	      POST_KERNEL, CONFIG_CORESIGHT_XLNX_INIT_PRIORITY,
	      NULL);
#endif
