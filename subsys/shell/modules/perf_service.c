/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include <zephyr/pmu.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_string_conv.h>
#include <zephyr/sys/util.h>

struct perf_shell_ctx {
	bool session_ready;
	bool counting;
	uint32_t counter0_event;
};

static struct perf_shell_ctx perf_ctx = {
	.counter0_event = PMU_EVT_CPU_CYCLES,
};

static bool perf_name_match(const char *a, const char *b)
{
	size_t la = strlen(a);
	size_t lb = strlen(b);

	return (la == lb) && (strncasecmp(a, b, la) == 0);
}

/* Aliases accepted by "perf start -e <name>" (Arm architectural PMU codes in pmu.h). */
static const struct {
	const char *name;
	uint32_t code;
} perf_evt_aliases[] = {
	{ "cycles", PMU_EVT_CPU_CYCLES },
	{ "cpu_cycles", PMU_EVT_CPU_CYCLES },
	{ "instructions", PMU_EVT_INST_RETIRED },
	{ "inst_retired", PMU_EVT_INST_RETIRED },
	{ "branch-misses", PMU_EVT_BR_MIS_PRED },
	{ "br_mis_pred", PMU_EVT_BR_MIS_PRED },
	{ "branches", PMU_EVT_BR_PRED },
	{ "br_pred", PMU_EVT_BR_PRED },
	{ "mem_access", PMU_EVT_MEM_ACCESS },
	{ "mem-access", PMU_EVT_MEM_ACCESS },
	{ "bus_access", PMU_EVT_BUS_ACCESS },
	{ "bus-access", PMU_EVT_BUS_ACCESS },
	{ "bus_cycles", PMU_EVT_BUS_CYCLES },
	{ "l1d_cache_refill", PMU_EVT_L1D_CACHE_REFILL },
	{ "l1i_cache_refill", PMU_EVT_L1I_CACHE_REFILL },
	{ "l1d_cache", PMU_EVT_L1D_CACHE },
	{ "l1i_cache", PMU_EVT_L1I_CACHE },
	{ "l2d_cache", PMU_EVT_L2D_CACHE },
	{ "l2d_cache_refill", PMU_EVT_L2D_CACHE_REFILL },
	{ "sw_incr", PMU_EVT_SW_INCR },
	{ "exc_taken", PMU_EVT_EXC_TAKEN },
	{ "exc_return", PMU_EVT_EXC_RETURN },
	{ "inst_spec", PMU_EVT_INST_SPEC },
	{ "ttbr_write", PMU_EVT_TTBR_WRITE },
	{ "memory_error", PMU_EVT_MEMORY_ERROR },
	{ "l1d_tlb_refill", PMU_EVT_L1D_TLB_REFILL },
	{ "l1i_tlb_refill", PMU_EVT_L1I_TLB_REFILL },
	{ "l1d_cache_wb", PMU_EVT_L1D_CACHE_WB },
	{ "l2d_cache_wb", PMU_EVT_L2D_CACHE_WB },
};

static bool perf_event_is_help(const char *name)
{
	return strcasecmp(name, "help") == 0 || strcmp(name, "?") == 0;
}

static void perf_print_event_list(const struct shell *sh)
{
	shell_print(sh, "perf start -e <name>   (or raw code 0x00-0x1f, hardware may filter)");
	shell_print(sh, "code  event             -e names (synonyms on one line)");

	for (size_t i = 0; i < ARRAY_SIZE(perf_evt_aliases); i++) {
		uint32_t code = perf_evt_aliases[i].code;
		char names[192];
		size_t pos = 0;
		bool skip;

		/* One row per distinct PMU code; parsing still accepts every alias below. */
		skip = false;
		for (size_t k = 0; k < i; k++) {
			if (perf_evt_aliases[k].code == code) {
				skip = true;
				break;
			}
		}
		if (skip) {
			continue;
		}

		names[0] = '\0';
		for (size_t j = 0; j < ARRAY_SIZE(perf_evt_aliases); j++) {
			int n;

			if (perf_evt_aliases[j].code != code) {
				continue;
			}
			n = snprintf(names + pos, sizeof(names) - pos, "%s%s",
				     pos > 0 ? ", " : "", perf_evt_aliases[j].name);
			if (n <= 0 || (size_t)n >= sizeof(names) - pos) {
				break;
			}
			pos += (size_t)n;
		}

		shell_print(sh, "0x%02x  %-18s  %s", code, pmu_event_name(code), names);
	}
}

static int cmd_perf_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	perf_print_event_list(sh);
	return 0;
}

static int perf_parse_event(const char *name, uint32_t *evt)
{
	int err = 0;
	unsigned long v;

	if (name[0] == '0' && name[1] == 'x') {
		v = shell_strtoul(name, 16, &err);
		if (err != 0) {
			return -EINVAL;
		}
		if (v > 0xFFFFFFFFUL) {
			return -EINVAL;
		}
		*evt = (uint32_t)v;
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(perf_evt_aliases); i++) {
		if (perf_name_match(name, perf_evt_aliases[i].name)) {
			*evt = perf_evt_aliases[i].code;
			return 0;
		}
	}

	return -EINVAL;
}

static int cmd_perf_start(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t evt = PMU_EVT_CPU_CYCLES;
	int ret;

	ret = pmu_init();
	if (ret != 0) {
		shell_error(sh, "PMU not available (%d)", ret);
		return ret;
	}

	if (pmu_num_counters() == 0U) {
		shell_error(sh, "No PMU event counters");
		return -ENOTSUP;
	}

	if (argc == 3) {
		if (strcmp(argv[1], "-e") != 0) {
			shell_error(sh, "Usage: perf start [-e <event>|help]");
			shell_print(sh, "Try: perf list");
			return -EINVAL;
		}
		if (perf_event_is_help(argv[2])) {
			perf_print_event_list(sh);
			return 0;
		}
		ret = perf_parse_event(argv[2], &evt);
		if (ret != 0) {
			shell_error(sh, "Unknown event: %s (try: perf list)", argv[2]);
			return -EINVAL;
		}
	} else if (argc != 1) {
		shell_error(sh, "Usage: perf start [-e <event>|help]");
		shell_print(sh, "Try: perf list");
		return -EINVAL;
	}

	pmu_stop();
	pmu_counter_disable_all();
	pmu_counter_reset_all();
	pmu_cycle_reset();

	ret = pmu_counter_config(0U, evt);
	if (ret != 0) {
		shell_error(sh, "pmu_counter_config failed (%d)", ret);
		return ret;
	}

	pmu_counter_enable(0U);
	pmu_start();

	perf_ctx.counter0_event = evt;
	perf_ctx.session_ready = true;
	perf_ctx.counting = true;

	shell_print(sh, "perf: counter0=%s (PMCCNTR + EVTEN0 running)",
		    pmu_event_name(evt));
	return 0;
}

static int cmd_perf_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (pmu_num_counters() == 0U) {
		(void)pmu_init();
		if (pmu_num_counters() == 0U) {
			shell_error(sh, "PMU not available");
			return -ENOTSUP;
		}
	}

	pmu_stop();
	perf_ctx.counting = false;
	shell_print(sh, "perf: stopped (counters frozen)");

	return 0;
}

static int cmd_perf_report(const struct shell *sh, size_t argc, char **argv)
{
	uint64_t cy;
	uint64_t c0;
	const char *evname;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!perf_ctx.session_ready) {
		shell_warn(sh, "perf: no session; run perf start first");
	}

	evname = pmu_event_name(perf_ctx.counter0_event);
	cy = pmu_cycle_count();
	c0 = pmu_counter_read(0U);

	shell_print(sh, "PMCCNTR_EL0:   %llu", (unsigned long long)cy);
	shell_print(sh, "counter0 (%s): %llu", evname, (unsigned long long)c0);

	if (perf_ctx.counting) {
		shell_print(sh, "(counting active; perf stop to freeze)");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_perf,
	SHELL_CMD_ARG(start, NULL,
		      "Start PMU: perf start [-e <ev>|help]; default -e cycles; see perf list",
		      cmd_perf_start, 1, 2),
	SHELL_CMD(list, NULL, "List names for perf start -e", cmd_perf_list),
	SHELL_CMD(stop, NULL, "Stop PMU (freeze counters)", cmd_perf_stop),
	SHELL_CMD(report, NULL, "Print PMCCNTR and programmable counter 0", cmd_perf_report),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(perf, &sub_perf,
		   "PMU perf commands: start/stop/report/list (requires HAS_PMU)", NULL);
