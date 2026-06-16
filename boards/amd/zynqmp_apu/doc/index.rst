.. Copyright (c) 2026 Advanced Micro Devices, Inc.
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:board:: zynqmp_apu

Overview
********

This board targets the Cortex-A53 application processing unit (APU) on AMD
Zynq UltraScale+ MPSoC devices. GIC-400 for the APU cluster is at
``0xf9010000``, high DDR at ``0x8_0000_0000``, and PS UART0 at ``0xff000000``.

Hardware
********

Supported Features
==================

.. zephyr:board-supported-hw::

The default console is UART0 (typical Linux name ``ttyPS0``).

Memories
--------

* ``sram0`` uses the high DDR window (2 GiB from ``0x800000000``).

Known limitations
=================

* Only CPU0 is supported by this configuration.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Build (example):

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: zynqmp_apu
   :goals: build

On hardware, Zephyr is intended to be loaded after Arm Trusted Firmware (TF-A) /
platform firmware has initialized the PS and provides PSCI via SMC. The Zephyr build
also produces TF-A ``bl31.elf`` (under ``build/tfa/zynqmp/``) and packages the
preloaded BL33 (Zephyr) into ``fip.bin`` when ``CONFIG_BUILD_WITH_TFA`` is enabled.

XSDB flash (cold boot with bitstream, PMUFW, FSBL, and BL31):

.. code-block:: console

   west flash --runner xsdb --bitstream <bit> --fsbl <fsbl.elf> --pmufw <pmufw.elf>

OpenOCD GDB attach (after XSDB flash; uses SDK ``xilinx_zynqmp.cfg``):

.. code-block:: console

   west debugserver

On a systest board farm, tunnel hw_server GDB with
``zephyr-board-extensions/scripts/openocd_remote.sh`` and use
``xsdb_flash_halt.cfg`` for halt-at-entry debug. For full JTAG SMP access via
OpenOCD, use ``openocd_xvc_farm.cfg`` when the lab provides an XVC endpoint.
See ``.claude/README-zynqmp-apu-farm-debug.md``.

References
**********

1. AMD Zynq UltraScale+ Device Technical Reference Manual (UG1085)
