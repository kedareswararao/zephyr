# Copyright (c) 2026 Advanced Micro Devices, Inc.
#
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BUILD_WITH_TFA)
  if(CONFIG_SRAM_DEPRECATED_KCONFIG_SET)
    set(RAM_ADDR ${CONFIG_SRAM_BASE_ADDRESS})
  else()
    dt_chosen(chosen_sram_path PROPERTY "zephyr,sram")
    dt_reg_addr(RAM_ADDR PATH "${chosen_sram_path}")
  endif()

  set(TFA_PLAT "zynqmp")
  # TFA_NO_PM=1 selects plat_psci_nopmu.c over plat_psci.c.
  # plat_psci_nopmu.c brings up secondary CPUs by directly writing the RVBAR
  # registers and clearing the per-core bits in CRF_APB_RST_FPD_APU, with no
  # IPI round-trip to PMUFW. plat_psci.c routes CPU_ON through pm_req_wakeup()
  # but discards its return value, so a silent PMUFW failure causes Zephyr to
  # hang indefinitely in the WFE loop waiting for a core that never starts.
  set(TFA_EXTRA_ARGS "PRELOADED_BL33_BASE=${RAM_ADDR}" "TFA_NO_PM=1")
  if(CONFIG_TFA_MAKE_BUILD_TYPE_DEBUG)
    set(BUILD_FOLDER "debug")
  else()
    set(BUILD_FOLDER "release")
  endif()
  set(XSDB_BL31_PATH ${PROJECT_BINARY_DIR}/../tfa/zynqmp/${BUILD_FOLDER}/bl31/bl31.elf)
  board_runner_args(xsdb "--bl31=${XSDB_BL31_PATH}")
endif()

include(${ZEPHYR_BASE}/boards/common/xsdb.board.cmake)
