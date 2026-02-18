# ARM CoreSight Tracing for Zephyr RTOS

## Table of Contents

1. [What is CoreSight](#1-what-is-coresight)
2. [Driver Architecture](#2-driver-architecture)
3. [End-to-End Solution](#3-end-to-end-solution)
4. [Verification with OpenCSD](#4-verification-with-opencsd)
5. [Comparison with Linux CoreSight](#5-comparison-with-linux-coresight)
6. [Quick Start Guide](#6-quick-start-guide)
7. [References](#7-references)

---

## 1. What is CoreSight

### 1.1 Overview

ARM CoreSight is an on-chip debug and trace architecture that provides non-intrusive visibility into processor execution. Unlike traditional debugging methods (breakpoints, single-stepping), CoreSight captures a continuous stream of execution data without stopping the processor, making it ideal for:

- **Performance profiling** - Identify hot paths and bottlenecks
- **Code coverage** - Verify which code paths were executed
- **Bug hunting** - Trace execution leading up to crashes
- **Security analysis** - Understand runtime behavior

### 1.2 CoreSight Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         Cortex-A78 CPU                          │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    ETM (Embedded Trace Macrocell)        │   │
│  │  - Monitors instruction execution                        │   │
│  │  - Generates compressed trace packets                    │   │
│  │  - Configurable filtering (address ranges, events)       │   │
│  └──────────────────────────┬──────────────────────────────┘   │
│                              │ ATB (AMBA Trace Bus)             │
└──────────────────────────────┼──────────────────────────────────┘
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│                     Trace Funnel                                  │
│  - Merges trace from multiple ETMs (multi-core)                  │
│  - Assigns trace IDs to distinguish sources                      │
└──────────────────────────────┬───────────────────────────────────┘
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│           TMC (Trace Memory Controller) - ETF/ETR Mode           │
│  ┌─────────────────────┐    ┌──────────────────────────────┐    │
│  │   ETF Mode          │    │   ETR Mode                   │    │
│  │   (Embedded Trace   │    │   (Embedded Trace Router)    │    │
│  │    FIFO)            │    │                              │    │
│  │   - 4KB internal    │    │   - Routes to external DDR   │    │
│  │     SRAM buffer     │    │   - Larger buffers possible  │    │
│  │   - Low latency     │    │   - Requires DRAM access     │    │
│  └─────────────────────┘    └──────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

### 1.3 How ETM Tracing Works

The ETMv4 (Embedded Trace Macrocell version 4) generates instruction trace using a highly compressed format:

#### Trace Generation Process

1. **Instruction Monitoring**: ETM watches every instruction executed by the CPU
2. **Waypoint Detection**: ETM identifies "waypoints" - instructions that change program flow:
   - Branch instructions (conditional/unconditional)
   - Exception entries/returns
   - Load/store to PC

3. **Compression**: Instead of recording every instruction address, ETM only records:
   - **Atoms** (E/N): Whether branches were taken (E=Executed/taken) or not (N=Not taken)
   - **Addresses**: Full or partial addresses only when needed (after branches, exceptions)
   - **Context**: Processor state changes (EL level, NS bit, ISA mode)

4. **Synchronization**: Periodic sync packets ensure decoder can resync if trace is corrupted

#### Example: Tracing a Loop

```c
for (int i = 0; i < 100; i++) {
    counter += i;
}
```

**Without ETM compression**: 100 × (instruction addresses) = ~800 bytes

**With ETM compression**:
```
ASYNC (12 bytes)           - Sync pattern
TRACE_INFO (3 bytes)       - Decoder sync point
ADDR_CTXT (10 bytes)       - Initial address + context
ATOM: E,E,E,E,...,E,N      - 99 E's (loop back) + 1 N (exit)
                           - Packed: ~25 bytes
```
Total: ~50 bytes (16x compression)

### 1.4 CoreSight Trace Packet Types

| Packet | Description |
|--------|-------------|
| **ASYNC** | 11 bytes of 0x00 + 0x80 - alignment/sync |
| **TRACE_INFO** | Trace configuration and sync point |
| **TRACE_ON** | Trace capture started |
| **ADDR_CTXT** | Full address with processor context |
| **ADDR_SHORT** | Compressed address (fewer bytes) |
| **ATOM** | Branch outcomes (E=taken, N=not-taken) |
| **EXCEPTION** | Exception entry/return |
| **TIMESTAMP** | Time reference |
| **CCNT** | Cycle count |

---

## 2. Driver Architecture

### 2.1 Driver Components

```
┌─────────────────────────────────────────────────────────────────┐
│                    coresight_xlnx.c                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ ETM Control  │  │ Funnel       │  │ TMC/ETF      │          │
│  │              │  │ Control      │  │ Control      │          │
│  │ - Program    │  │              │  │              │          │
│  │   filters    │  │ - Enable     │  │ - Configure  │          │
│  │ - Set trace  │  │   ports      │  │   buffer     │          │
│  │   ID         │  │ - Route      │  │ - Drain      │          │
│  │ - Start/     │  │   trace      │  │   data       │          │
│  │   stop       │  │              │  │              │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Shared Functions                       │  │
│  │  - cs_unlock()/cs_lock() - LAR/LSR access control        │  │
│  │  - cs_claim_device()     - Software lock management      │  │
│  │  - tmc_wait_for_sync()   - Wait for sync packets         │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Register Access Flow

CoreSight components are memory-mapped and protected by lock registers:

```c
// Unlock component for programming
static void cs_unlock(mem_addr_t base)
{
    sys_write32(CORESIGHT_UNLOCK_VALUE, base + CS_LAR);  // Write 0xC5ACCE55
}

// Lock component after programming  
static void cs_lock(mem_addr_t base)
{
    sys_write32(0x0, base + CS_LAR);
}
```

### 2.3 ETM Configuration

The ETM is programmed with trace parameters:

```c
static int etm_configure(void)
{
    cs_unlock(etm_base);
    
    // 1. Disable ETM for programming
    etm_write(etm_base, TRCPRGCTLR, 0);
    
    // 2. Wait for idle
    while (!(etm_read(etm_base, TRCSTATR) & TRCSTATR_IDLE));
    
    // 3. Configure trace parameters
    etm_write(etm_base, TRCCONFIGR, 
              TRCCONFIGR_INSTP0_LOAD_STORE |  // Trace load/store as P0
              TRCCONFIGR_BB);                  // Branch broadcast
    
    // 4. Set trace ID (unique per ETM)
    etm_write(etm_base, TRCTRACEIDR, CONFIG_CORESIGHT_XLNX_TRACE_ID);
    
    // 5. Configure sync frequency (every 256 bytes)
    etm_write(etm_base, TRCSYNCPR, 0x8);
    
    // 6. Enable all tracing (ViewInst = trace everything)
    etm_write(etm_base, TRCVICTLR, 
              TRCVICTLR_SSSTATUS |             // Start/stop status
              TRCVICTLR_EVENT_SEL(0) |         // Event 0
              TRCVICTLR_TRCERR |               // Trace system errors
              TRCVICTLR_TRCRESET |             // Trace reset
              TRCVICTLR_EXLEVEL_NS_EL1);       // Trace NS EL1
    
    cs_lock(etm_base);
    return 0;
}
```

### 2.4 TMC/ETF Buffer Management

The TMC in ETF (FIFO) mode provides a 4KB circular buffer:

```c
static int tmc_configure_etf(void)
{
    cs_unlock(tmc_base);
    
    // 1. Disable TMC
    sys_write32(0, tmc_base + TMC_CTL);
    while (sys_read32(tmc_base + TMC_STS) & TMC_STS_TMCREADY == 0);
    
    // 2. Set to circular buffer mode
    sys_write32(TMC_MODE_CIRCULAR, tmc_base + TMC_MODE);
    
    // 3. Configure formatter (include trace IDs)
    sys_write32(TMC_FFCR_ENFT | TMC_FFCR_ENTI, tmc_base + TMC_FFCR);
    
    // 4. Enable TMC
    sys_write32(TMC_CTL_TRACECAPTEN, tmc_base + TMC_CTL);
    
    cs_lock(tmc_base);
    return 0;
}
```

### 2.5 Trace Data Extraction

When tracing stops, data is read from the ETF's internal FIFO:

```c
static size_t tmc_etf_drain(uint8_t *buffer, size_t max_size)
{
    size_t bytes_read = 0;
    
    cs_unlock(tmc_base);
    
    // Put TMC in "stopped" mode to read data
    sys_write32(0, tmc_base + TMC_CTL);
    while (!(sys_read32(tmc_base + TMC_STS) & TMC_STS_TMCREADY));
    
    // Enable drain mode
    sys_write32(TMC_FFCR_DRAINBUFFER, tmc_base + TMC_FFCR);
    
    // Read data via RRD register (each read = 4 bytes)
    while (bytes_read < max_size) {
        uint32_t data = sys_read32(tmc_base + TMC_RRD);
        if (data == 0xFFFFFFFF) break;  // Buffer empty
        
        memcpy(buffer + bytes_read, &data, 4);
        bytes_read += 4;
    }
    
    cs_lock(tmc_base);
    return bytes_read;
}
```

---

## 3. End-to-End Solution

### 3.1 System Overview

```
┌────────────────────────────────────────────────────────────────────┐
│                         Zephyr Application                         │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │  main()                                                       │ │
│  │    │                                                          │ │
│  │    ├──► coresight_xlnx_init()     // Initialize hardware     │ │
│  │    │         │                                                │ │
│  │    │         ├──► Map registers via DTS                      │ │
│  │    │         ├──► Configure ETM (trace ID, filters)          │ │
│  │    │         ├──► Configure Funnel (enable port)             │ │
│  │    │         └──► Configure TMC/ETF (buffer mode)            │ │
│  │    │                                                          │ │
│  │    ├──► coresight_xlnx_start()    // Begin tracing           │ │
│  │    │         │                                                │ │
│  │    │         ├──► Enable TMC capture                         │ │
│  │    │         ├──► Enable ETM (write TRCPRGCTLR)              │ │
│  │    │         └──► Wait for sync packets                      │ │
│  │    │                                                          │ │
│  │    ├──► [Application code executes - BEING TRACED]           │ │
│  │    │         │                                                │ │
│  │    │         └──► test_function_a(), test_function_b(), ...  │ │
│  │    │                                                          │ │
│  │    ├──► coresight_xlnx_stop()     // Stop tracing            │ │
│  │    │         │                                                │ │
│  │    │         ├──► Disable ETM                                │ │
│  │    │         ├──► Flush formatter                            │ │
│  │    │         └──► Stop TMC capture                           │ │
│  │    │                                                          │ │
│  │    ├──► coresight_xlnx_dump_etm_config()  // Print config    │ │
│  │    │                                                          │ │
│  │    └──► k_sys_halt()              // Wait for XSDB dump      │ │
│  │                                                               │ │
│  └──────────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────┐
│                    Hardware (VersalNet SoC)                        │
│                                                                    │
│   ETM @ 0xF0B4C000                                                │
│     └──► Generates trace packets in real-time                     │
│                                                                    │
│   Funnel @ 0xF0B08000                                             │
│     └──► Routes CPU0's trace to TMC                               │
│                                                                    │
│   TMC/ETF @ 0xF0B0C000                                            │
│     └──► Stores trace in 4KB internal SRAM (address 0x1B000)      │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────┐
│                      XSDB (Host Debugger)                          │
│                                                                    │
│   mrd -bin -file trace.bin 0x1b000 252                            │
│     └──► Dumps raw trace data from ETF buffer to file             │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────┐
│                      OpenCSD Decoder (Host)                        │
│                                                                    │
│   trc_pkt_lister -ss_dir snapshot -decode                         │
│     └──► Decodes trace.bin → instruction addresses + branches     │
│                                                                    │
└────────────────────────────────────────────────────────────────────┘
```

### 3.2 Application Flow (main.c)

```c
int main(void)
{
    int ret;
    
    printk("CoreSight Trace Sample\n");
    
    // Step 1: Initialize CoreSight subsystem
    ret = coresight_xlnx_init();
    if (ret) {
        printk("Failed to initialize CoreSight: %d\n", ret);
        return ret;
    }
    
    // Step 2: Print ETM configuration for OpenCSD
    coresight_xlnx_dump_etm_config();
    
    // Step 3: Start tracing
    ret = coresight_xlnx_start();
    if (ret) {
        printk("Failed to start tracing: %d\n", ret);
        return ret;
    }
    
    // ============================================
    // Step 4: Execute code to be traced
    // ============================================
    run_test_workload();  // This code is being traced
    
    // ============================================
    
    // Step 5: Stop tracing
    ret = coresight_xlnx_stop();
    
    // Step 6: Print XSDB command for trace dump
    print_xsdb_command();
    
    // Step 7: Halt for manual trace extraction
    printk("System halted - ready for XSDB memory dump.\n");
    k_sys_halt();
    
    return 0;
}
```

### 3.3 Driver Operations Sequence

| Step | Application | Driver (coresight_xlnx.c) | Hardware |
|------|-------------|---------------------------|----------|
| 1 | `coresight_xlnx_init()` | Map DTS registers, claim devices | - |
| 2 | | Configure ETM: trace ID, filters | ETM programmed |
| 3 | | Configure Funnel: enable CPU0 port | Funnel routes trace |
| 4 | | Configure TMC: ETF circular buffer | TMC ready |
| 5 | `coresight_xlnx_start()` | Enable TMC capture | TMC capturing |
| 6 | | Enable ETM tracing | ETM generating packets |
| 7 | | Wait for sync packet | ASYNC+TRACE_INFO sent |
| 8 | `run_test_workload()` | - | Trace packets → ETF buffer |
| 9 | `coresight_xlnx_stop()` | Disable ETM | ETM stops |
| 10 | | Flush formatter | Pending packets flushed |
| 11 | | Stop TMC | Buffer frozen |
| 12 | XSDB dump | - | Raw data extracted |

### 3.4 Data Flow

```
┌─────────────┐    ┌──────────────┐    ┌──────────────┐    ┌─────────────┐
│   CPU       │    │    ETM       │    │   Funnel     │    │   TMC/ETF   │
│             │    │              │    │              │    │             │
│ Executes    │───►│ Generates    │───►│ Routes to    │───►│ Stores in   │
│ instructions│    │ trace        │    │ sink         │    │ 4KB SRAM    │
│             │    │ packets      │    │              │    │             │
└─────────────┘    └──────────────┘    └──────────────┘    └─────────────┘
                                                                  │
                                                                  ▼
┌─────────────┐    ┌──────────────┐    ┌──────────────┐    ┌─────────────┐
│   Host PC   │◄───│    JTAG      │◄───│   XSDB       │◄───│  Memory     │
│   .bin file │    │   Transfer   │    │   Dump       │    │  0x1B000    │
└─────────────┘    └──────────────┘    └──────────────┘    └─────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           OpenCSD Decoder                               │
│                                                                         │
│  Raw packets ──► Packet Decoder ──► Instruction Decoder ──► Output     │
│                                                                         │
│  0x00 0x00 ...   I_ASYNC           INSTR_RANGE              0x1234:    │
│  0x01 0x01 0x00  I_TRACE_INFO      0x1234-0x1238            0x1238     │
│  0xFD            I_ATOM E,N,E      num_i=2, BR taken        ...        │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Verification with OpenCSD

### 4.1 What is OpenCSD?

OpenCSD (Open CoreSight Decoder) is Linaro's open-source library for decoding ARM CoreSight trace data. It:

- Parses raw trace packets (ASYNC, ATOM, ADDR, etc.)
- Correlates with binary images for instruction decode
- Outputs human-readable execution flow

### 4.2 Snapshot Directory Structure

OpenCSD requires a "snapshot" directory with configuration files:

```
opencsd_snapshot/
├── snapshot.ini      # Lists all devices and trace metadata
├── trace.ini         # Maps trace sources to buffers
├── cpu_0.ini         # CPU configuration + memory image
├── device_0.ini      # ETM register values
├── cstrace.bin       # Raw trace data from hardware
└── zephyr.bin        # Binary image for instruction decode
```

### 4.3 Key Configuration Files

#### snapshot.ini
```ini
[snapshot]
version=1.0

[device_list]
device0=cpu_0.ini      # Core definition
device1=device_0.ini   # ETM definition

[trace]
metadata=trace.ini     # Trace buffer mappings
```

#### trace.ini
```ini
[trace_buffers]
buffers=buffer0

[buffer0]
name=ETB_0
file=cstrace.bin       # Raw trace data file
format=coresight       # CoreSight framed format

[source_buffers]
ETM_0=ETB_0            # ETM_0's trace goes to ETB_0

[core_trace_sources]
cpu_0=ETM_0            # cpu_0 is traced by ETM_0
```

#### device_0.ini (ETM registers)
```ini
[device]
name=ETM_0
class=trace_source
type=ETM4

[regs]
# Register format: NAME(word_offset)=value
TRCCONFIGR(0x004)=0x00001001    # Trace config
TRCTRACEIDR(0x010)=0x00000001   # Trace ID = 1
TRCIDR0(0x078)=0x28000EA1       # ETM capabilities
TRCIDR1(0x079)=0x4100F422       # Architecture version
...
```

### 4.4 Decoder Output Interpretation

```
Idx:12; ID:1; I_TRACE_INFO : Trace Info.; INFO=0x0
Idx:17; ID:1; I_TRACE_ON : Trace On.
Idx:18; ID:1; I_ADDR_CTXT_L_64IS0 : Address & Context; Addr=0x6484; Ctxt: AArch64,EL1,NS
Idx:28; ID:1; I_ATOM_F3 : Atom format 3.; ENE
Idx:17; ID:1; OCSD_GEN_TRC_ELEM_TRACE_ON( [begin or filter])
Idx:18; ID:1; OCSD_GEN_TRC_ELEM_PE_CONTEXT((ISA=A64) EL1N; 64-bit; )
Idx:28; ID:1; OCSD_GEN_TRC_ELEM_INSTR_RANGE(exec range=0x6484:[0x648c] num_i(2) E BR)
```

| Field | Meaning |
|-------|---------|
| `Idx:12` | Byte index in trace buffer |
| `ID:1` | Trace ID (identifies which ETM) |
| `I_TRACE_INFO` | Packet type (raw) |
| `OCSD_GEN_TRC_ELEM_*` | Decoded element |
| `exec range=0x6484:[0x648c]` | Instructions at 0x6484-0x648c executed |
| `num_i(2)` | 2 instructions in this range |
| `E BR` | Ended with taken branch |

### 4.5 Verification Script

The `verify_trace.sh` script automates validation:

```bash
./scripts/verify_trace.sh /path/to/snapshot /path/to/zephyr.elf
```

**Checks performed:**

1. **Trace Data Integrity**
   - File size > 16 bytes
   - ASYNC sync pattern present

2. **Decoder Configuration**
   - Protocol printer created
   - Valid trace ID detected

3. **Trace Content**
   - TRACE_INFO sync point
   - TRACE_ON packet
   - Instruction ranges decoded

4. **Symbol Correlation**
   - Traced addresses mapped to functions
   - Expected test functions present

5. **Branch Analysis**
   - E/N atom counts
   - Taken/not-taken ratio

---

## 5. Comparison with Linux CoreSight

### 5.1 Architecture Comparison

| Aspect | Linux CoreSight | Zephyr CoreSight |
|--------|-----------------|------------------|
| **Driver Model** | Kernel subsystem with sysfs interface | Single monolithic driver |
| **Component Discovery** | Device Tree + AMBA bus probe | Static DTS configuration |
| **Trace Sink** | perf subsystem integration | Direct buffer access |
| **User Interface** | `perf record` / sysfs | API calls + XSDB |
| **Decoder** | perf + OpenCSD | Standalone OpenCSD |

### 5.2 Linux CoreSight Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Linux Kernel                                    │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                    CoreSight Subsystem                             │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐              │ │
│  │  │ ETM     │  │ Funnel  │  │ TMC     │  │ CTI     │   ...        │ │
│  │  │ Driver  │  │ Driver  │  │ Driver  │  │ Driver  │              │ │
│  │  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘              │ │
│  │       │            │            │            │                    │ │
│  │  ┌────┴────────────┴────────────┴────────────┴──────────────────┐│ │
│  │  │              CoreSight Bus / Topology Manager                 ││ │
│  │  │  - Discovers components via AMBA/Device Tree                  ││ │
│  │  │  - Builds trace path graphs                                   ││ │
│  │  │  - Manages enable/disable sequences                           ││ │
│  │  └──────────────────────────────────────────────────────────────┘│ │
│  │                              │                                    │ │
│  │  ┌───────────────────────────┴────────────────────────────────┐  │ │
│  │  │                    perf subsystem                           │  │ │
│  │  │  - perf_event interface                                     │  │ │
│  │  │  - AUX buffer management                                    │  │ │
│  │  │  - Integrated with perf tool                                │  │ │
│  │  └────────────────────────────────────────────────────────────┘  │ │
│  └───────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         User Space                                      │
│                                                                         │
│  $ perf record -e cs_etm/@tmc_etf0/ -- ./my_program                    │
│  $ perf report --itrace=i100                                           │
│                                                                         │
│  Or direct sysfs access:                                               │
│  $ echo 1 > /sys/bus/coresight/devices/etm0/enable_source              │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.3 Zephyr CoreSight Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Zephyr RTOS                                     │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                  coresight_xlnx.c Driver                           │ │
│  │                                                                    │ │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐                   │ │
│  │  │ ETM        │  │ Funnel     │  │ TMC/ETF    │                   │ │
│  │  │ Functions  │  │ Functions  │  │ Functions  │                   │ │
│  │  └────────────┘  └────────────┘  └────────────┘                   │ │
│  │         │               │               │                          │ │
│  │  ┌──────┴───────────────┴───────────────┴───────────────────────┐ │ │
│  │  │              Direct Register Access (MMIO)                    │ │ │
│  │  │  - Addresses from Device Tree                                 │ │ │
│  │  │  - No component discovery                                     │ │ │
│  │  │  - Static configuration                                       │ │ │
│  │  └──────────────────────────────────────────────────────────────┘ │ │
│  └───────────────────────────────────────────────────────────────────┘ │
│                                                                         │
│  ┌───────────────────────────────────────────────────────────────────┐ │
│  │                    Application Code                                │ │
│  │                                                                    │ │
│  │  coresight_xlnx_init();                                           │ │
│  │  coresight_xlnx_start();                                          │ │
│  │  // ... traced code ...                                           │ │
│  │  coresight_xlnx_stop();                                           │ │
│  │                                                                    │ │
│  └───────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         External Tools                                  │
│                                                                         │
│  XSDB: mrd -bin -file trace.bin 0x1b000 252                            │
│  OpenCSD: trc_pkt_lister -ss_dir snapshot -decode                      │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.4 Key Differences

| Feature | Linux | Zephyr |
|---------|-------|--------|
| **Trace Capture** | `perf record` with AUX buffers | API calls + XSDB dump |
| **Real-time** | perf streams to userspace | Post-mortem only |
| **Buffer Size** | Large (configurable, DDR) | 4KB (ETF internal) |
| **Multi-core** | Full topology support | Single core |
| **Filtering** | Runtime via perf | Compile-time config |
| **Decoding** | `perf script` with OpenCSD | Standalone OpenCSD |
| **Overhead** | Higher (kernel + perf) | Lower (direct HW access) |

### 5.5 Linux Usage Example

```bash
# List available CoreSight devices
$ ls /sys/bus/coresight/devices/
etm0  funnel0  replicator0  tmc_etf0  tmc_etr0

# Record trace with perf
$ perf record -e cs_etm/@tmc_etr0/u -- ./benchmark

# Decode and report
$ perf report --itrace=i100us --stdio
```

### 5.6 Zephyr Usage Example

```c
// In application code
coresight_xlnx_init();
coresight_xlnx_start();
run_benchmark();
coresight_xlnx_stop();
// System halts - use XSDB to dump trace
```

```bash
# On host
$ xsdb
xsdb% mrd -bin -file trace.bin 0x1b000 252

# Decode
$ trc_pkt_lister -ss_dir snapshot -decode -logstdout
```

### 5.7 When to Use Which

**Use Linux CoreSight when:**
- Full OS environment available
- Need real-time trace streaming
- Large trace buffers required
- Complex filtering needed
- Multi-core tracing required

**Use Zephyr CoreSight when:**
- Bare-metal or RTOS environment
- Minimal overhead critical
- Post-mortem analysis acceptable
- Simple single-core tracing
- Early boot/firmware tracing

---

## 6. Quick Start Guide

### 6.1 Build the Sample

```bash
cd $ZEPHYR_BASE
west build -b versal2_apu samples/drivers/coresight
```

### 6.2 Run on Hardware

```bash
# Connect XSDB
xsdb% connect
xsdb% targets -set -filter {name =~ "Versal *"}
xsdb% device program build/zephyr/zephyr.elf
xsdb% con
```

### 6.3 Capture Trace

When the sample prints the XSDB command, copy and execute it:
```
xsdb% mrd -bin -file trace.bin 0x1b000 252
```

### 6.4 Decode Trace

```bash
# Set up library path
export LD_LIBRARY_PATH=/proj/xhdsswstaff/appanad/OpenCSD/decoder/lib/builddir

# Run verification script
./scripts/verify_trace.sh ./opencsd_snapshot build/zephyr/zephyr.elf

# Or decode directly
/proj/xhdsswstaff/appanad/OpenCSD/decoder/tests/bin/builddir/trc_pkt_lister \
    -ss_dir opencsd_snapshot -decode -logstdout | less
```

---

## 7. References

### ARM Documentation
- [ARM IHI 0064H - ETMv4 Architecture Specification](https://developer.arm.com/documentation/ihi0064)
- [ARM IHI 0029E - CoreSight Architecture Specification](https://developer.arm.com/documentation/ihi0029)
- [ARM DDI 0480 - CoreSight TMC Technical Reference Manual](https://developer.arm.com/documentation/ddi0480)

### OpenCSD
- [OpenCSD GitHub Repository](https://github.com/Linaro/OpenCSD)
- [OpenCSD Programmer's Guide](https://github.com/Linaro/OpenCSD/blob/master/decoder/docs/prog_guide/)

### Linux CoreSight
- [Linux Kernel CoreSight Documentation](https://www.kernel.org/doc/html/latest/trace/coresight/coresight.html)
- [ARM CoreSight in Linux](https://developer.arm.com/tools-and-software/embedded/coresight)

### Xilinx/AMD
- [Versal ACAP Technical Reference Manual](https://docs.xilinx.com/r/en-US/am011-versal-acap-trm)

---

## License

Apache-2.0
