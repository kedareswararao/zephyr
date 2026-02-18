/*
 * Copyright (c) 2026 AMD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DEBUG_CORESIGHT_H_
#define ZEPHYR_INCLUDE_DRIVERS_DEBUG_CORESIGHT_H_

/**
 * @file
 * @brief CoreSight trace API
 *
 * Public API for CoreSight trace functionality on AMD Xilinx platforms.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup coresight_api CoreSight Trace API
 * @ingroup debug_interfaces
 * @{
 */

/**
 * @brief Initialize CoreSight tracing subsystem
 *
 * This function initializes the CoreSight components including:
 * - ETM (Embedded Trace Macrocell) for CPU instruction tracing
 * - TMC/ETR (Trace Memory Controller) for trace capture
 * - Funnel for trace merging from multiple sources
 *
 * @return 0 on success, negative errno on failure
 * @retval -ENOTSUP Trace not supported on this platform
 * @retval -ENOMEM Memory allocation failed
 */
int coresight_xlnx_init(void);

/**
 * @brief Start CoreSight tracing
 *
 * Enables trace capture. After this call, CPU instruction trace
 * will be collected into the trace buffer.
 *
 * @return 0 on success, negative errno on failure
 * @retval -ENODEV CoreSight not initialized
 */
int coresight_xlnx_start(void);

/**
 * @brief Stop CoreSight tracing
 *
 * Disables trace capture and flushes any pending trace data.
 *
 * @return 0 on success, negative errno on failure
 * @retval -ENODEV CoreSight not initialized
 */
int coresight_xlnx_stop(void);

/**
 * @brief Get trace buffer information
 *
 * Returns pointers to the trace buffer and its size.
 *
 * @param buffer Pointer to store trace buffer address (can be NULL)
 * @param size Pointer to store buffer size in bytes (can be NULL)
 * @return 0 on success, negative errno on failure
 * @retval -ENODEV CoreSight not initialized
 */
int coresight_xlnx_get_trace_buffer(uint8_t **buffer, size_t *size);

/**
 * @brief Get current trace write offset
 *
 * Returns the current write pointer offset within the trace buffer.
 * This indicates how much trace data has been captured.
 *
 * @return Offset in bytes, or negative errno on failure
 * @retval -ENODEV CoreSight not initialized
 */
int coresight_xlnx_get_trace_offset(void);

/**
 * @brief Check if trace buffer has wrapped
 *
 * In circular buffer mode, the buffer wraps when full.
 * This function checks if wrapping has occurred.
 *
 * @return true if buffer has wrapped, false otherwise
 */
bool coresight_xlnx_trace_wrapped(void);

/**
 * @brief Dump ETM configuration for OpenCSD
 *
 * Prints the ETM register values in OpenCSD device.ini format
 * for offline trace decoding with tools like trc_pkt_lister.
 */
void coresight_xlnx_dump_etm_config(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_DEBUG_CORESIGHT_H_ */
