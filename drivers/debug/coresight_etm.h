/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CORESIGHT_ETM_H_
#define CORESIGHT_ETM_H_

/**
 * @file
 * @brief ARM CoreSight ETMv4 (Embedded Trace Macrocell) register definitions
 *
 * This header provides register definitions for ETMv4.x and ETE (Embedded Trace Extension)
 * for ARM Cortex-A processors. These are used for CPU instruction tracing.
 *
 * Reference: ARM IHI 0064H - ETMv4 Architecture Specification
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ETMv4 Register Offsets (Memory-mapped interface)
 * All offsets are from the ETM base address
 */

/* Programming registers */
#define TRCPRGCTLR		0x004	/* Programming Control Register */
#define TRCPROCSELR		0x008	/* PE Select Control Register */
#define TRCSTATR		0x00C	/* Trace Status Register */
#define TRCCONFIGR		0x010	/* Trace Configuration Register */
#define TRCAUXCTLR		0x018	/* Auxiliary Control Register */
#define TRCEVENTCTL0R		0x020	/* Event Control 0 Register */
#define TRCEVENTCTL1R		0x024	/* Event Control 1 Register */
#define TRCSTALLCTLR		0x02C	/* Stall Control Register */
#define TRCTSCTLR		0x030	/* Global Timestamp Control Register */
#define TRCSYNCPR		0x034	/* Synchronization Period Register */
#define TRCCCCTLR		0x038	/* Cycle Count Control Register */
#define TRCBBCTLR		0x03C	/* Branch Broadcast Control Register */
#define TRCTRACEIDR		0x040	/* Trace ID Register */
#define TRCQCTLR		0x044	/* Q Element Control Register */

/* Filtering control registers */
#define TRCVICTLR		0x080	/* ViewInst Main Control Register */
#define TRCVIIECTLR		0x084	/* ViewInst Include/Exclude Control Register */
#define TRCVISSCTLR		0x088	/* ViewInst Start/Stop Control Register */
#define TRCVIPCSSCTLR		0x08C	/* ViewInst Start/Stop PE Comparator Control */

/* Derived resource registers */
#define TRCVDCTLR		0x0A0	/* ViewData Main Control Register */
#define TRCVDSACCTLR		0x0A4	/* ViewData Include/Exclude Single Address */
#define TRCVDARCCTLR		0x0A8	/* ViewData Include/Exclude Address Range */

/* Sequencer registers */
#define TRCSEQEVR(n)		(0x100 + (n) * 4)	/* Sequencer State Transition Event */
#define TRCSEQRSTEVR		0x118	/* Sequencer Reset Control Register */
#define TRCSEQSTR		0x11C	/* Sequencer State Register */
#define TRCEXTINSELR		0x120	/* External Input Select Register */

/* Counter registers */
#define TRCCNTRLDVR(n)		(0x140 + (n) * 4)	/* Counter Reload Value */
#define TRCCNTCTLR(n)		(0x150 + (n) * 4)	/* Counter Control Register */
#define TRCCNTVR(n)		(0x160 + (n) * 4)	/* Counter Value Register */

/* ID registers */
#define TRCIDR0			0x1E0	/* ID Register 0 */
#define TRCIDR1			0x1E4	/* ID Register 1 */
#define TRCIDR2			0x1E8	/* ID Register 2 */
#define TRCIDR3			0x1EC	/* ID Register 3 */
#define TRCIDR4			0x1F0	/* ID Register 4 */
#define TRCIDR5			0x1F4	/* ID Register 5 */
#define TRCIDR6			0x1F8	/* ID Register 6 (Reserved) */
#define TRCIDR7			0x1FC	/* ID Register 7 (Reserved) */
#define TRCIDR8			0x180	/* ID Register 8 */
#define TRCIDR9			0x184	/* ID Register 9 */
#define TRCIDR10		0x188	/* ID Register 10 */
#define TRCIDR11		0x18C	/* ID Register 11 */
#define TRCIDR12		0x190	/* ID Register 12 */
#define TRCIDR13		0x194	/* ID Register 13 */

/* Resource selection registers */
#define TRCRSCTLR(n)		(0x200 + (n) * 4)	/* Resource Selection Control */

/* Single-shot control registers */
#define TRCSSCCR(n)		(0x280 + (n) * 4)	/* Single-shot Comparator Control */
#define TRCSSCSR(n)		(0x2A0 + (n) * 4)	/* Single-shot Comparator Status */
#define TRCSSPCICR(n)		(0x2C0 + (n) * 4)	/* Single-shot PE Comparator Input */

/* OS Lock registers */
#define TRCOSLAR		0x300	/* OS Lock Access Register */
#define TRCOSLSR		0x304	/* OS Lock Status Register */

/* Power down registers */
#define TRCPDCR			0x310	/* Power Down Control Register */
#define TRCPDSR			0x314	/* Power Down Status Register */

/* Address comparator registers */
#define TRCACVR(n)		(0x400 + (n) * 8)	/* Address Comparator Value */
#define TRCACATR(n)		(0x480 + (n) * 8)	/* Address Comparator Access Type */

/* Data value comparator registers */
#define TRCDVCVR(n)		(0x500 + (n) * 16)	/* Data Value Comparator Value */
#define TRCDVCMR(n)		(0x580 + (n) * 16)	/* Data Value Comparator Mask */

/* Context ID comparator registers */
#define TRCCIDCVR(n)		(0x600 + (n) * 8)	/* Context ID Comparator Value */
#define TRCCIDCCTLR0		0x680	/* Context ID Comparator Control 0 */
#define TRCCIDCCTLR1		0x684	/* Context ID Comparator Control 1 */

/* VMID comparator registers */
#define TRCVMIDCVR(n)		(0x640 + (n) * 8)	/* VMID Comparator Value */
#define TRCVMIDCCTLR0		0x688	/* VMID Comparator Control 0 */
#define TRCVMIDCCTLR1		0x68C	/* VMID Comparator Control 1 */

/* Implementation defined registers */
#define TRCIMSPEC(n)		(0x1C0 + (n) * 4)	/* Implementation Specific */

/* Integration registers */
#define TRCITCTRL		0xF00	/* Integration Mode Control */
#define TRCCLAIMSET		0xFA0	/* Claim Tag Set Register */
#define TRCCLAIMCLR		0xFA4	/* Claim Tag Clear Register */
#define TRCAUTHSTATUS		0xFB8	/* Authentication Status Register */

/* Device architecture registers */
#define TRCDEVARCH		0xFBC	/* Device Architecture Register */
#define TRCDEVID		0xFC8	/* Device Configuration Register */
#define TRCDEVTYPE		0xFCC	/* Device Type Identifier Register */

/* Peripheral ID registers */
#define TRCPIDR0		0xFE0
#define TRCPIDR1		0xFE4
#define TRCPIDR2		0xFE8
#define TRCPIDR3		0xFEC
#define TRCPIDR4		0xFD0
#define TRCPIDR5		0xFD4
#define TRCPIDR6		0xFD8
#define TRCPIDR7		0xFDC

/* Component ID registers */
#define TRCCIDR0		0xFF0
#define TRCCIDR1		0xFF4
#define TRCCIDR2		0xFF8
#define TRCCIDR3		0xFFC

/*
 * ETMv4 Register Bit Definitions
 */

/* TRCPRGCTLR - Programming Control Register */
#define TRCPRGCTLR_EN		BIT(0)	/* Trace unit enable */

/* TRCSTATR - Trace Status Register */
#define TRCSTATR_IDLE		BIT(0)	/* Trace unit idle */
#define TRCSTATR_PMSTABLE	BIT(1)	/* Programmers' model stable */

/* TRCCONFIGR - Trace Configuration Register */
#define TRCCONFIGR_INSTP0_LOAD	(0x1 << 1)	/* Trace load instructions */
#define TRCCONFIGR_INSTP0_STORE	(0x2 << 1)	/* Trace store instructions */
#define TRCCONFIGR_INSTP0_BOTH	(0x3 << 1)	/* Trace load and store */
#define TRCCONFIGR_BB		BIT(3)	/* Branch broadcast enable */
#define TRCCONFIGR_CCI		BIT(4)	/* Cycle counting enable */
#define TRCCONFIGR_CID		BIT(6)	/* Context ID tracing enable */
#define TRCCONFIGR_VMID		BIT(7)	/* VMID tracing enable */
#define TRCCONFIGR_COND_MASK	(0x7 << 8)	/* Conditional tracing */
#define TRCCONFIGR_TS		BIT(11)	/* Global timestamp enable */
#define TRCCONFIGR_RS		BIT(12)	/* Return stack enable */
#define TRCCONFIGR_QE_MASK	(0x3 << 13)	/* Q element enable */
#define TRCCONFIGR_VMIDOPT	BIT(15)	/* VMID options */

/* TRCVICTLR - ViewInst Main Control Register */
#define TRCVICTLR_EVENT_MASK	(0xFF << 0)	/* Event selector */
#define TRCVICTLR_SSSTATUS	BIT(9)	/* Start/Stop status */
#define TRCVICTLR_TRCRESET	BIT(10)	/* Trace on reset */
#define TRCVICTLR_TRCERR	BIT(11)	/* Trace on error */
#define TRCVICTLR_EXLEVEL_S_MASK	(0xF << 16)	/* Secure EL filtering */
#define TRCVICTLR_EXLEVEL_NS_MASK	(0xF << 20)	/* Non-secure EL filtering */

/* TRCOSLSR - OS Lock Status Register */
#define TRCOSLSR_OSLM_MASK	(0x9)	/* OS Lock Model */
#define TRCOSLSR_LOCKED		BIT(1)	/* OS Lock status */
#define TRCOSLSR_OSLK		BIT(1)	/* OS Lock status (same as LOCKED) */

/* TRCPDCR - Power Down Control Register */
#define TRCPDCR_PU		BIT(3)	/* Power up request */

/* TRCDEVARCH - Device Architecture Register */
#define TRCDEVARCH_ARCHID_MASK	(0xFFFF)
#define TRCDEVARCH_ETMv4	0x4A13	/* ETMv4 architecture */
#define TRCDEVARCH_ETE		0x5A13	/* ETE architecture */

/* TRCIDR0 bit fields */
#define TRCIDR0_INSTP0_MASK	(0x3 << 1)
#define TRCIDR0_TRCBB		BIT(5)
#define TRCIDR0_TRCCOND		BIT(6)
#define TRCIDR0_TRCCCI		BIT(7)
#define TRCIDR0_RETSTACK	BIT(9)
#define TRCIDR0_NUMEVENT_MASK	(0x3 << 10)
#define TRCIDR0_QSUPP_MASK	(0x3 << 15)
#define TRCIDR0_QFILT		BIT(14)
#define TRCIDR0_TSSIZE_MASK	(0x1F << 24)

/* TRCIDR3 bit fields */
#define TRCIDR3_CCITMIN_MASK	(0xFFF)
#define TRCIDR3_EXLEVEL_S_MASK	(0xF << 16)
#define TRCIDR3_EXLEVEL_NS_MASK	(0xF << 20)
#define TRCIDR3_TRCERR		BIT(24)
#define TRCIDR3_SYNCPR		BIT(25)
#define TRCIDR3_STALLCTL	BIT(26)
#define TRCIDR3_SYSSTALL	BIT(27)
#define TRCIDR3_NOOVERFLOW	BIT(31)

/* TRCIDR4 bit fields */
#define TRCIDR4_NUMACPAIRS_MASK	(0xF << 0)
#define TRCIDR4_NUMPC_MASK	(0xF << 12)
#define TRCIDR4_NUMRSPAIR_MASK	(0xF << 16)
#define TRCIDR4_NUMSSCC_MASK	(0xF << 20)
#define TRCIDR4_NUMCIDC_MASK	(0xF << 24)
#define TRCIDR4_NUMVMIDC_MASK	(0xF << 28)

/* TRCIDR5 bit fields */
#define TRCIDR5_NUMEXTIN_MASK	(0x1FF << 0)
#define TRCIDR5_TRACEIDSIZE_MASK	(0x3F << 16)
#define TRCIDR5_ATBTRIG		BIT(22)
#define TRCIDR5_LPOVERRIDE	BIT(23)
#define TRCIDR5_NUMSEQSTATE_MASK	(0x7 << 25)
#define TRCIDR5_NUMCNTR_MASK	(0x7 << 28)

/*
 * ETM Configuration Structures
 */

/**
 * @brief ETM trace configuration
 */
struct etm_config {
	uint32_t mode;		/**< Tracing mode flags */
	uint32_t cfg;		/**< TRCCONFIGR value */
	uint32_t eventctrl0;	/**< Event control 0 */
	uint32_t eventctrl1;	/**< Event control 1 */
	uint32_t stall_ctrl;	/**< Stall control */
	uint32_t ts_ctrl;	/**< Timestamp control */
	uint32_t syncfreq;	/**< Sync frequency */
	uint32_t ccctlr;	/**< Cycle count control */
	uint32_t bb_ctrl;	/**< Branch broadcast control */
	uint32_t vinst_ctrl;	/**< ViewInst control */
	uint32_t viiectlr;	/**< ViewInst include/exclude */
	uint32_t vissctlr;	/**< ViewInst start/stop */
	uint8_t trace_id;	/**< Trace ID for this source */
};

/**
 * @brief ETM hardware capabilities
 */
struct etm_caps {
	uint32_t arch;		/**< Architecture version */
	uint32_t nr_addr_cmp;	/**< Number of address comparators */
	uint32_t nr_cntr;	/**< Number of counters */
	uint32_t nr_resource;	/**< Number of resource selectors */
	uint32_t nr_ss_cmp;	/**< Number of single-shot comparators */
	uint32_t nr_pe_cmp;	/**< Number of PE comparators */
	uint32_t numcidc;	/**< Number of context ID comparators */
	uint32_t numvmidc;	/**< Number of VMID comparators */
	uint32_t ccitmin;	/**< Minimum cycle count threshold */
	bool trcbb;		/**< Branch broadcast support */
	bool trccond;		/**< Conditional tracing support */
	bool trccci;		/**< Cycle counting support */
	bool retstack;		/**< Return stack support */
	bool stallctl;		/**< Stall control support */
	bool nooverflow;	/**< No-overflow support */
	bool ts_size;		/**< Timestamp size (bits) */
};

#ifdef __cplusplus
}
#endif

#endif /* CORESIGHT_ETM_H_ */
