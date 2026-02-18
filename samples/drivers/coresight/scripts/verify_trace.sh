#!/bin/bash
#
# verify_trace.sh - Verify CoreSight trace output against ELF symbols
#
# Usage: ./verify_trace.sh <snapshot_dir> [zephyr.elf]
#
# This script:
# 1. Runs OpenCSD decoder on the snapshot
# 2. Extracts traced addresses from output
# 3. Compares with symbols from ELF
# 4. Reports which functions were traced
#

set -e

# Tool paths
NM="/proj/ssw_xhd/drivers/appanad/zephyr_common_sdk/github_qemu/zephyr-sdk-0.16.8/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-nm"
OBJDUMP="/proj/ssw_xhd/drivers/appanad/zephyr_common_sdk/github_qemu/zephyr-sdk-0.16.8/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-objdump"
TRC_PKT_LISTER="/proj/xhdsswstaff/appanad/OpenCSD/decoder/tests/bin/builddir/trc_pkt_lister"

# OpenCSD library path
export LD_LIBRARY_PATH="/proj/xhdsswstaff/appanad/OpenCSD/decoder/lib/builddir:$LD_LIBRARY_PATH"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_ok() {
    echo -e "${GREEN}[OK]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
}

# Parse arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <snapshot_dir> [zephyr.elf]"
    echo ""
    echo "Arguments:"
    echo "  snapshot_dir  - OpenCSD snapshot directory"
    echo "  zephyr.elf    - ELF file (optional, will look in snapshot_dir)"
    exit 1
fi

SNAPSHOT_DIR="$1"
ELF_FILE="${2:-}"

# Find ELF file if not specified
if [ -z "$ELF_FILE" ]; then
    if [ -f "$SNAPSHOT_DIR/zephyr.elf" ]; then
        ELF_FILE="$SNAPSHOT_DIR/zephyr.elf"
    elif [ -f "$SNAPSHOT_DIR/../../../build/zephyr/zephyr.elf" ]; then
        ELF_FILE="$SNAPSHOT_DIR/../../../build/zephyr/zephyr.elf"
    else
        echo "Error: Could not find zephyr.elf. Please specify path."
        exit 1
    fi
fi

# Verify files exist
for f in "$SNAPSHOT_DIR/cstrace.bin" "$SNAPSHOT_DIR/snapshot.ini" "$ELF_FILE"; do
    if [ ! -f "$f" ]; then
        print_fail "File not found: $f"
        exit 1
    fi
done

# Verify tools exist
for tool in "$NM" "$TRC_PKT_LISTER"; do
    if [ ! -x "$tool" ]; then
        print_fail "Tool not found or not executable: $tool"
        exit 1
    fi
done

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

print_header "CoreSight Trace Verification"
echo "Snapshot: $SNAPSHOT_DIR"
echo "ELF: $ELF_FILE"
echo ""

# Step 1: Check trace file
print_header "Step 1: Checking Trace Data"
TRACE_SIZE=$(stat -c %s "$SNAPSHOT_DIR/cstrace.bin")
echo "Trace file size: $TRACE_SIZE bytes"

if [ $TRACE_SIZE -lt 16 ]; then
    print_fail "Trace file too small (< 16 bytes)"
    exit 1
fi

# Check for sync pattern (11 bytes of 0x00 followed by 0x80)
FIRST_BYTES=$(xxd -l 16 "$SNAPSHOT_DIR/cstrace.bin" | head -1)
echo "First 16 bytes: $FIRST_BYTES"

if echo "$FIRST_BYTES" | grep -q "0000 0000 0000 0000 0000 00.*80"; then
    print_ok "ASYNC sync pattern found at start"
else
    print_warn "ASYNC sync pattern not at start - trace may have wrapped"
fi

# Step 2: Extract symbols from ELF
print_header "Step 2: Extracting Symbols from ELF"
$NM -n "$ELF_FILE" 2>/dev/null | grep -E " [Tt] " > "$TEMP_DIR/symbols.txt"
SYMBOL_COUNT=$(wc -l < "$TEMP_DIR/symbols.txt")
echo "Found $SYMBOL_COUNT text symbols"

# Show expected test functions
echo ""
echo "Expected test functions:"
grep -E "test_function_|main|coresight" "$TEMP_DIR/symbols.txt" | head -20 || echo "  (none found)"

# Step 3: Run decoder
print_header "Step 3: Running OpenCSD Decoder"
$TRC_PKT_LISTER -ss_dir "$SNAPSHOT_DIR" -decode -logstdout 2>&1 > "$TEMP_DIR/decode_output.txt"

# Check for errors
if grep -q "OCSD_ERR" "$TEMP_DIR/decode_output.txt"; then
    print_warn "Decoder reported errors:"
    grep "OCSD_ERR" "$TEMP_DIR/decode_output.txt" | head -5
fi

# Check for protocol printer
if grep -q "Protocol printer ETMV4I" "$TEMP_DIR/decode_output.txt"; then
    TRACE_ID=$(grep "Protocol printer ETMV4I" "$TEMP_DIR/decode_output.txt" | sed 's/.*ID 0x/0x/')
    print_ok "Protocol printer created for Trace ID $TRACE_ID"
else
    print_fail "No protocol printer created - check device_0.ini configuration"
    exit 1
fi

# Check for sync
if grep -q "I_ASYNC" "$TEMP_DIR/decode_output.txt"; then
    print_ok "ASYNC synchronization packet found"
else
    print_fail "No ASYNC packet - trace not synchronized"
fi

if grep -q "I_TRACE_INFO" "$TEMP_DIR/decode_output.txt"; then
    print_ok "TRACE_INFO packet found - decoder sync point"
else
    print_warn "No TRACE_INFO packet"
fi

if grep -q "I_TRACE_ON" "$TEMP_DIR/decode_output.txt"; then
    print_ok "TRACE_ON packet found - trace capture started"
else
    print_warn "No TRACE_ON packet"
fi

# Step 4: Extract traced addresses
print_header "Step 4: Analyzing Traced Addresses"

# Extract instruction range addresses
grep -oE "exec range=0x[0-9a-fA-F]+:\[0x[0-9a-fA-F]+\]" "$TEMP_DIR/decode_output.txt" | \
    sed 's/exec range=0x\([0-9a-fA-F]*\):\[0x\([0-9a-fA-F]*\)\]/\1 \2/' > "$TEMP_DIR/ranges.txt"

RANGE_COUNT=$(wc -l < "$TEMP_DIR/ranges.txt")
echo "Found $RANGE_COUNT instruction ranges in trace"

if [ $RANGE_COUNT -eq 0 ]; then
    # Check for ADDR_NACC (address no access)
    NACC_COUNT=$(grep -c "ADDR_NACC" "$TEMP_DIR/decode_output.txt" || echo 0)
    if [ "$NACC_COUNT" -gt 0 ]; then
        print_warn "Found $NACC_COUNT addresses with no memory access"
        print_warn "Memory accessor may not be configured correctly"
        echo ""
        echo "Check cpu_0.ini:"
        echo "  - file= should point to zephyr.bin"
        echo "  - length= should match the binary size"
        echo "  - address= should be 0x0 for Zephyr"
    fi
fi

# Extract unique start addresses
cut -d' ' -f1 "$TEMP_DIR/ranges.txt" | sort -u > "$TEMP_DIR/traced_addrs.txt"
UNIQUE_ADDRS=$(wc -l < "$TEMP_DIR/traced_addrs.txt")
echo "Found $UNIQUE_ADDRS unique traced address ranges"

# Step 5: Map addresses to functions
print_header "Step 5: Mapping Addresses to Functions"

echo ""
echo "Traced functions:"
echo "-----------------"

# Create a simple address-to-function lookup using awk (much faster)
# For each traced address, find the function it belongs to
awk '
NR==FNR {
    # Read symbols file - store function addresses
    if ($2 == "T" || $2 == "t") {
        addr = strtonum("0x" $1)
        funcs[addr] = $3
        addrs[++n] = addr
    }
    next
}
{
    # Read traced addresses
    traced_addr = strtonum("0x" $1)
    
    # Binary search would be ideal, but linear is fine for small lists
    best_match = ""
    best_addr = 0
    for (i = 1; i <= n; i++) {
        if (addrs[i] <= traced_addr && addrs[i] > best_addr) {
            best_addr = addrs[i]
            best_match = funcs[addrs[i]]
        }
    }
    if (best_match != "") {
        print best_match
    }
}
' "$TEMP_DIR/symbols.txt" "$TEMP_DIR/traced_addrs.txt" | sort -u > "$TEMP_DIR/matched_funcs.txt"

# Show unique matched functions
if [ -s "$TEMP_DIR/matched_funcs.txt" ]; then
    while read -r func; do
        # Get address of function
        func_addr=$(grep " $func$" "$TEMP_DIR/symbols.txt" 2>/dev/null | awk '{print $1}' | head -1)
        echo "  $func (0x$func_addr)"
    done < "$TEMP_DIR/matched_funcs.txt"
    
    FUNC_COUNT=$(wc -l < "$TEMP_DIR/matched_funcs.txt")
    echo ""
    echo "Total: $FUNC_COUNT unique functions traced"
else
    print_warn "Could not map any addresses to functions"
fi

# Step 6: Verify expected functions
print_header "Step 6: Verifying Expected Test Functions"

EXPECTED_FUNCS="test_function_a test_function_b test_function_c run_test_workload main"
FOUND_COUNT=0
MISSING_COUNT=0

for func in $EXPECTED_FUNCS; do
    if [ -f "$TEMP_DIR/matched_funcs.txt" ] && grep -q "$func" "$TEMP_DIR/matched_funcs.txt"; then
        print_ok "Found: $func"
        ((FOUND_COUNT++)) || true
    else
        # Check if function exists in ELF
        if grep -q " $func$" "$TEMP_DIR/symbols.txt"; then
            print_warn "Missing: $func (exists in ELF but not in trace)"
            ((MISSING_COUNT++)) || true
        fi
    fi
done

# Step 7: Analyze branch patterns
print_header "Step 7: Analyzing Branch Patterns"

# Count atom types
E_COUNT=$(grep -oE "I_ATOM_F[0-9].*E" "$TEMP_DIR/decode_output.txt" | grep -oE "E" | wc -l || echo 0)
N_COUNT=$(grep -oE "I_ATOM_F[0-9].*N" "$TEMP_DIR/decode_output.txt" | grep -oE "N" | wc -l || echo 0)

echo "Taken branches (E atoms): $E_COUNT"
echo "Not-taken branches (N atoms): $N_COUNT"

if [ $E_COUNT -gt 0 ] || [ $N_COUNT -gt 0 ]; then
    TOTAL=$((E_COUNT + N_COUNT))
    E_PCT=$((E_COUNT * 100 / TOTAL))
    echo "Branch taken ratio: ${E_PCT}%"
    print_ok "Branch data present in trace"
else
    print_warn "No branch atoms found"
fi

# Step 8: Summary
print_header "Verification Summary"

PASS=0
FAIL=0

if grep -q "I_ASYNC" "$TEMP_DIR/decode_output.txt"; then
    ((PASS++)) || true
else
    ((FAIL++)) || true
fi

if grep -q "Protocol printer" "$TEMP_DIR/decode_output.txt"; then
    ((PASS++)) || true
else
    ((FAIL++)) || true
fi

if [ $RANGE_COUNT -gt 0 ]; then
    ((PASS++)) || true
else
    ((FAIL++)) || true
fi

if [ $E_COUNT -gt 0 ] || [ $N_COUNT -gt 0 ]; then
    ((PASS++)) || true
else
    ((FAIL++)) || true
fi

echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All checks passed!${NC}"
    echo "Trace appears to be valid and properly decoded."
else
    echo -e "${YELLOW}$PASS checks passed, $FAIL checks failed${NC}"
    echo "Review warnings above for issues."
fi

echo ""
echo "For detailed decode output, run:"
echo "  $TRC_PKT_LISTER -ss_dir $SNAPSHOT_DIR -decode -logstdout | less"
