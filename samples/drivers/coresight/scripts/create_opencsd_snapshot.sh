#!/bin/bash
#
# create_opencsd_snapshot.sh - Create OpenCSD snapshot directory for trace decoding
#
# Usage: ./create_opencsd_snapshot.sh <trace.bin> <zephyr.elf> <etm_config_file> [output_dir]
#
# Arguments:
#   trace.bin       - Raw trace data dumped from XSDB
#   zephyr.elf      - Zephyr ELF file for symbols
#   etm_config_file - File containing ETM register output from console
#   output_dir      - Output directory (default: ./opencsd_snapshot)
#

set -e

if [ $# -lt 3 ]; then
    echo "Usage: $0 <trace.bin> <zephyr.elf> <etm_config_file> [output_dir]"
    echo ""
    echo "Arguments:"
    echo "  trace.bin       - Raw trace data from XSDB"
    echo "  zephyr.elf      - Zephyr ELF file"
    echo "  etm_config_file - File with ETM register values from console"
    echo "  output_dir      - Output directory (default: ./opencsd_snapshot)"
    exit 1
fi

TRACE_BIN="$1"
ZEPHYR_ELF="$2"
ETM_CONFIG="$3"
OUTPUT_DIR="${4:-./opencsd_snapshot}"

# Verify input files
for f in "$TRACE_BIN" "$ZEPHYR_ELF" "$ETM_CONFIG"; do
    if [ ! -f "$f" ]; then
        echo "Error: File not found: $f"
        exit 1
    fi
done

# Create output directory
mkdir -p "$OUTPUT_DIR"
echo "Creating OpenCSD snapshot in: $OUTPUT_DIR"

# Copy trace data
cp "$TRACE_BIN" "$OUTPUT_DIR/cstrace.bin"
echo "  Copied trace data: $(stat -c %s "$OUTPUT_DIR/cstrace.bin") bytes"

# Convert ELF to binary
if command -v aarch64-zephyr-elf-objcopy &> /dev/null; then
    OBJCOPY=aarch64-zephyr-elf-objcopy
elif command -v aarch64-none-elf-objcopy &> /dev/null; then
    OBJCOPY=aarch64-none-elf-objcopy
else
    # Try to find it from the build directory
    ZEPHYR_BIN="${ZEPHYR_ELF%.elf}.bin"
    if [ -f "$ZEPHYR_BIN" ]; then
        cp "$ZEPHYR_BIN" "$OUTPUT_DIR/zephyr.bin"
        echo "  Copied binary: $(stat -c %s "$OUTPUT_DIR/zephyr.bin") bytes"
    else
        echo "Warning: objcopy not found and zephyr.bin not present"
        echo "  Memory accessor will not work for symbol resolution"
        touch "$OUTPUT_DIR/zephyr.bin"
    fi
fi

if [ -n "$OBJCOPY" ]; then
    $OBJCOPY -O binary "$ZEPHYR_ELF" "$OUTPUT_DIR/zephyr.bin"
    echo "  Created binary: $(stat -c %s "$OUTPUT_DIR/zephyr.bin") bytes"
fi

# Get binary size for cpu_0.ini
BIN_SIZE=$(stat -c %s "$OUTPUT_DIR/zephyr.bin")
BIN_SIZE_HEX=$(printf "0x%X" $BIN_SIZE)

# Get entry point
if command -v nm &> /dev/null; then
    ENTRY=$(nm "$ZEPHYR_ELF" 2>/dev/null | grep "T __start" | awk '{print "0x"$1}' || echo "0x1000")
else
    ENTRY="0x1000"
fi

# Create snapshot.ini
cat > "$OUTPUT_DIR/snapshot.ini" << 'EOF'
[snapshot]
version=1.0

[device_list]
device0=cpu_0.ini
device1=device_0.ini

[trace]
metadata=trace.ini
EOF
echo "  Created snapshot.ini"

# Create trace.ini
cat > "$OUTPUT_DIR/trace.ini" << 'EOF'
[trace_buffers]
buffers=buffer0

[buffer0]
name=ETB_0
file=cstrace.bin
format=coresight

[source_buffers]
ETM_0=ETB_0

[core_trace_sources]
cpu_0=ETM_0
EOF
echo "  Created trace.ini"

# Create cpu_0.ini
cat > "$OUTPUT_DIR/cpu_0.ini" << EOF
[device]
name=cpu_0
class=core
type=Cortex-A76

[regs]
PC(size:64)=$ENTRY
SP(size:64)=0x0
SCTLR_EL1=0x1007
CPSR=0x1C5

[dump1]
file=zephyr.bin
address=0x0
length=$BIN_SIZE_HEX
EOF
echo "  Created cpu_0.ini (entry=$ENTRY, length=$BIN_SIZE_HEX)"

# Extract ETM config from input file
# Look for lines matching the device.ini format
grep -E "^\[device\]|^name=|^class=|^type=|^\[regs\]|^TRC" "$ETM_CONFIG" > "$OUTPUT_DIR/device_0.ini" 2>/dev/null || {
    # If grep fails, create a template
    cat > "$OUTPUT_DIR/device_0.ini" << 'EOF'
[device]
name=ETM_0
class=trace_source
type=ETM4

[regs]
# Copy register values from console output here
EOF
    echo "  Warning: Could not extract ETM config. Edit device_0.ini manually."
}
echo "  Created device_0.ini"

echo ""
echo "Snapshot created successfully!"
echo ""
echo "To decode trace, run:"
echo "  trc_pkt_lister -ss_dir $OUTPUT_DIR -decode -logstdout"
echo ""
echo "If device_0.ini is empty, copy the [regs] section from console output."
