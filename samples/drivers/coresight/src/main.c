/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief CoreSight Trace Sample Application
 *
 * This sample demonstrates how to use the CoreSight driver for
 * CPU instruction tracing on AMD Xilinx Versal/VersalNet platforms.
 *
 * The sample:
 * 1. Initializes the CoreSight subsystem
 * 2. Starts trace capture
 * 3. Executes some test code
 * 4. Stops trace capture
 * 5. Displays trace buffer information
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/debug/coresight.h>
#include <string.h>
#include <stdio.h>

/* Test workload functions - these will be traced */

static volatile uint32_t test_counter;

static void __attribute__((noinline)) test_function_a(void)
{
	for (int i = 0; i < 100; i++) {
		test_counter += i;
	}
}

static void __attribute__((noinline)) test_function_b(int iterations)
{
	for (int i = 0; i < iterations; i++) {
		if (i % 2 == 0) {
			test_counter += i * 2;
		} else {
			test_counter -= i;
		}
	}
}

static uint32_t __attribute__((noinline)) test_function_c(uint32_t a, uint32_t b)
{
	uint32_t result = 0;

	for (uint32_t i = 0; i < a; i++) {
		result += b * i;
		if (result > 10000) {
			result = result % 1000;
		}
	}

	return result;
}

static void run_test_workload(void)
{
	printk("Running test workload...\n");

	test_function_a();
	test_function_b(50);
	test_counter += test_function_c(20, 5);

	/* Some conditional branches */
	for (int i = 0; i < 30; i++) {
		switch (i % 4) {
		case 0:
			test_function_a();
			break;
		case 1:
			test_function_b(10);
			break;
		case 2:
			test_counter += test_function_c(5, i);
			break;
		default:
			test_counter++;
			break;
		}
	}

	printk("Test workload complete. Counter = %u\n", test_counter);
}

static void dump_trace_buffer_info(void)
{
	uint8_t *buffer;
	size_t size;
	int offset;
	bool wrapped;
	int ret;

	ret = coresight_xlnx_get_trace_buffer(&buffer, &size);
	if (ret < 0) {
		printk("Failed to get trace buffer: %d\n", ret);
		return;
	}

	offset = coresight_xlnx_get_trace_offset();
	wrapped = coresight_xlnx_trace_wrapped();

	printk("\n=== Trace Buffer Information ===\n");
	printk("Buffer address: %p\n", buffer);
	printk("Buffer size: %zu bytes\n", size);
	printk("Write offset: %d bytes\n", offset);
	printk("Buffer wrapped: %s\n", wrapped ? "yes" : "no");

	if (offset > 0) {
		size_t trace_bytes = wrapped ? size : offset;
		printk("Trace data captured: %zu bytes\n", trace_bytes);

		/* Show first few bytes of trace data */
		printk("\nFirst 64 bytes of trace data:\n");
		for (int i = 0; i < 64 && i < (int)trace_bytes; i++) {
			printk("%02x ", buffer[i]);
			if ((i + 1) % 16 == 0) {
				printk("\n");
			}
		}
		printk("\n");
	} else {
		printk("No trace data captured\n");
	}
}

int main(void)
{
	int ret;

	printk("\n");
	printk("===================================================\n");
	printk("CoreSight Trace Sample for AMD Xilinx Versal/VersalNet\n");
	printk("===================================================\n\n");

	/* Initialize CoreSight */
	printk("Initializing CoreSight...\n");
	ret = coresight_xlnx_init();
	if (ret < 0) {
		printk("CoreSight initialization failed: %d\n", ret);
		printk("Note: This may be expected on platforms without CoreSight or QEMU\n");
		return ret;
	}
	printk("CoreSight initialized successfully\n\n");

	/* Start tracing */
	printk("Starting trace capture...\n");
	ret = coresight_xlnx_start();
	if (ret < 0) {
		printk("Failed to start tracing: %d\n", ret);
		return ret;
	}
	printk("Trace capture started\n\n");

	/* Run test workload */
	run_test_workload();

	/* Stop tracing */
	printk("\nStopping trace capture...\n");
	ret = coresight_xlnx_stop();
	if (ret < 0) {
		printk("Failed to stop tracing: %d\n", ret);
		return ret;
	}
	printk("Trace capture stopped\n");

	/* Display trace information */
	dump_trace_buffer_info();

	/* Variables for XSDB dump command */
	uint8_t *buffer;
	size_t size;
	int offset;

	printk("\n===================================================\n");
	printk("Sample complete!\n");
	printk("===================================================\n");

	/* Dump ETM configuration for OpenCSD decoding */
	coresight_xlnx_dump_etm_config();

	/* Print XSDB dump command LAST so it doesn't scroll away */
	if (coresight_xlnx_get_trace_buffer(&buffer, &size) == 0) {
		offset = coresight_xlnx_get_trace_offset();
		size_t trace_bytes = coresight_xlnx_trace_wrapped() ? size : offset;

		printk("\n###################################################\n");
		printk("# XSDB COMMAND TO DUMP TRACE (copy-paste this):\n");
		printk("###################################################\n");
		if (trace_bytes > 0) {
			printk("mrd -bin -file trace.bin 0x%lx %zu\n",
			       (unsigned long)(uintptr_t)buffer, (trace_bytes + 3) / 4);
		} else {
			printk("# No trace data captured\n");
			printk("# Buffer at 0x%lx, size %zu\n",
			       (unsigned long)(uintptr_t)buffer, size);
		}
		printk("###################################################\n");
	}

	printk("\nSystem halted - ready for XSDB memory dump.\n");
	while (1) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
