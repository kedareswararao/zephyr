# CoreSight Trace Sample for AMD Xilinx Versal/VersalNet

This sample demonstrates CPU instruction tracing using ARM CoreSight
on AMD Xilinx Versal and VersalNet platforms (Cortex-A78).

## Overview

CoreSight is ARM's debug and trace architecture that allows capturing
a record of CPU instruction execution. This is useful for:

- **Debugging**: Understand exactly what code executed
- **Profiling**: Identify hot code paths and bottlenecks
- **Coverage**: Verify which code paths were exercised
- **Post-mortem analysis**: Debug crashes by examining execution history

## Hardware Requirements

- AMD Xilinx Versal or VersalNet board with Cortex-A78 APU
- JTAG debugger (optional, for trace extraction)

## Building

```bash
# For Versal Net APU
west build -p -b amd_versal_net_app_amd_versal_net_apu samples/drivers/coresight

# For QEMU (trace capture will be limited)
west build -p -b qemu_cortex_a53 samples/drivers/coresight
```

## Running

```bash
west flash
# or
west build -t run
```

## Expected Output

```
===================================================
CoreSight Trace Sample for AMD Xilinx Versal/VersalNet
===================================================

Initializing CoreSight...
CoreSight initialized successfully

Starting trace capture...
Trace capture started

Running test workload...
Test workload complete. Counter = 12345

Stopping trace capture...
Trace capture stopped

=== Trace Buffer Information ===
Buffer address: 0x........
Buffer size: 65536 bytes
Write offset: 4096 bytes
Buffer wrapped: no
Trace data captured: 4096 bytes

First 64 bytes of trace data:
01 23 45 67 89 ab cd ef ...
===================================================
Sample complete!
```

## Decoding Trace Data

The captured trace data is in ARM CoreSight binary format. To decode:

1. **Extract trace buffer** via JTAG or memory dump
2. **Use OpenCSD library** to decode the trace:
   ```bash
   # Install OpenCSD
   git clone https://github.com/Linaro/OpenCSD.git
   cd OpenCSD/decoder/build/linux
   make

   # Decode trace
   ./bin/linux64/rel/trc_pkt_lister -i trace.bin -o decode.txt
   ```

3. **Or use perf** (on Linux host with ARM support):
   ```bash
   perf report --itrace=i100us -i perf.data
   ```

## Configuration Options

The following Kconfig options are available:

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_CORESIGHT_XLNX_BUFFER_SIZE` | 4096 | Trace buffer size in bytes |
| `CONFIG_CORESIGHT_XLNX_TRACE_ID` | 1 | Unique trace source ID |
| `CONFIG_CORESIGHT_XLNX_TIMESTAMP` | y | Enable timestamps |
| `CONFIG_CORESIGHT_XLNX_CYCLE_ACCURATE` | n | Enable cycle counting |
| `CONFIG_CORESIGHT_XLNX_BRANCH_BROADCAST` | n | Enable branch broadcast |

## API Reference

```c
/* Initialize CoreSight */
int coresight_xlnx_init(void);

/* Start trace capture */
int coresight_xlnx_start(void);

/* Stop trace capture */
int coresight_xlnx_stop(void);

/* Get trace buffer info */
int coresight_xlnx_get_trace_buffer(uint8_t **buffer, size_t *size);

/* Get current write offset */
int coresight_xlnx_get_trace_offset(void);

/* Check if buffer wrapped */
bool coresight_xlnx_trace_wrapped(void);
```

## Limitations

- QEMU does not emulate CoreSight components, so trace capture will not
  produce valid data on QEMU
- The trace buffer size is limited by available memory
- Full ETM configuration requires privileged access (EL2/EL3 setup)

## References

- ARM CoreSight Architecture: ARM IHI 0029
- ARM ETMv4 Architecture: ARM IHI 0064
- OpenCSD Trace Decoder: https://github.com/Linaro/OpenCSD
