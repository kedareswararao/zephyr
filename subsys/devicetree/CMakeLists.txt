# Copyright (c) 2026 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(reconfigurable.c)

zephyr_linker_sources(DATA_SECTIONS reconfigurable.ld)
zephyr_iterable_section(NAME dt_reconfigurable GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT})
