#!/bin/bash
#===============================================================================
# lwIP Build Validation Script
#
# Validates that the compiled firmware BSS size matches lwIP configuration.
# This catches common issues where lwipopts.h changes don't trigger recompile.
#
# Usage: validate_lwip_build.sh <elf_file> <lwipopts.h>
#
# Copyright (c) October 2025 Michael Wolak
#===============================================================================

set -e

# Check arguments
if [ $# -ne 2 ]; then
    echo "ERROR: Invalid arguments"
    echo "Usage: $0 <elf_file> <lwipopts.h>"
    exit 1
fi

ELF_FILE="$1"
LWIPOPTS_H="$2"

# Check if files exist
if [ ! -f "$ELF_FILE" ]; then
    echo "ERROR: ELF file not found: $ELF_FILE"
    exit 1
fi

if [ ! -f "$LWIPOPTS_H" ]; then
    echo "ERROR: lwipopts.h not found: $LWIPOPTS_H"
    exit 1
fi

# Detect toolchain prefix
if command -v riscv64-unknown-elf-size &> /dev/null; then
    SIZE_CMD="riscv64-unknown-elf-size"
elif command -v riscv32-unknown-elf-size &> /dev/null; then
    SIZE_CMD="riscv32-unknown-elf-size"
else
    echo "ERROR: RISC-V size command not found (riscv64-unknown-elf-size or riscv32-unknown-elf-size)"
    exit 1
fi

# Extract BSS size from ELF file
BSS_ACTUAL=$($SIZE_CMD "$ELF_FILE" | tail -n 1 | awk '{print $3}')

if [ -z "$BSS_ACTUAL" ] || [ "$BSS_ACTUAL" -eq 0 ] 2>/dev/null; then
    echo "ERROR: Failed to extract BSS size from $ELF_FILE"
    exit 1
fi

# Parse lwipopts.h for configuration values
PBUF_POOL_SIZE=$(grep -E "^#define\s+PBUF_POOL_SIZE\s+" "$LWIPOPTS_H" | awk '{print $3}')
PBUF_POOL_BUFSIZE=$(grep -E "^#define\s+PBUF_POOL_BUFSIZE\s+" "$LWIPOPTS_H" | awk '{print $3}')
MEMP_NUM_TCP_SEG=$(grep -E "^#define\s+MEMP_NUM_TCP_SEG\s+" "$LWIPOPTS_H" | awk '{print $3}')

if [ -z "$PBUF_POOL_SIZE" ] || [ -z "$PBUF_POOL_BUFSIZE" ]; then
    echo "ERROR: Failed to parse PBUF_POOL_SIZE or PBUF_POOL_BUFSIZE from $LWIPOPTS_H"
    exit 1
fi

# Calculate expected PBUF pool size in bytes
# The PBUF_POOL is allocated in BSS via MEMP system
# Each pbuf requires PBUF_POOL_BUFSIZE bytes, aligned
# Formula: PBUF_POOL_SIZE * (PBUF_POOL_BUFSIZE + alignment overhead)
# We'll use a conservative estimate with ~4 byte alignment per buffer

PBUF_POOL_BYTES=$((PBUF_POOL_SIZE * PBUF_POOL_BUFSIZE))

# Expected minimum BSS: pbuf pool + other lwIP structures
# Base BSS for non-lwIP is ~4KB, lwIP adds pbuf pool + other MEMP structures
# We expect BSS to be at least (4KB base + PBUF_POOL_BYTES)

BASE_BSS=4096
MIN_BSS_EXPECTED=$((BASE_BSS + PBUF_POOL_BYTES - 5000))  # Allow 5KB tolerance below
MAX_BSS_EXPECTED=$((BASE_BSS + PBUF_POOL_BYTES + 30000)) # Allow 30KB overhead for other MEMP pools

# Display configuration
echo "===================================="
echo "lwIP Build Validation"
echo "===================================="
echo "Configuration (from $LWIPOPTS_H):"
echo "  PBUF_POOL_SIZE      = $PBUF_POOL_SIZE buffers"
echo "  PBUF_POOL_BUFSIZE   = $PBUF_POOL_BUFSIZE bytes"
if [ -n "$MEMP_NUM_TCP_SEG" ]; then
    echo "  MEMP_NUM_TCP_SEG    = $MEMP_NUM_TCP_SEG segments"
fi
echo ""
echo "Memory Analysis:"
echo "  PBUF pool size      = $((PBUF_POOL_BYTES / 1024)) KB (${PBUF_POOL_SIZE} × ${PBUF_POOL_BUFSIZE} bytes)"
echo "  Expected BSS range  = $((MIN_BSS_EXPECTED / 1024))-$((MAX_BSS_EXPECTED / 1024)) KB"
echo "  Actual BSS size     = $((BSS_ACTUAL / 1024)) KB ($BSS_ACTUAL bytes)"
echo ""

# Validate BSS is in expected range
if [ "$BSS_ACTUAL" -lt "$MIN_BSS_EXPECTED" ]; then
    echo "❌ VALIDATION FAILED: BSS size is too small!"
    echo ""
    echo "Expected at least $((MIN_BSS_EXPECTED / 1024)) KB, got $((BSS_ACTUAL / 1024)) KB"
    echo ""
    echo "This usually means lwIP source files were not recompiled after"
    echo "lwipopts.h was modified. Try:"
    echo ""
    echo "  cd firmware"
    echo "  find ../downloads/lwip -name '*.o' -delete"
    echo "  make clean"
    echo "  make <your_target>"
    echo ""
    exit 1
fi

if [ "$BSS_ACTUAL" -gt "$MAX_BSS_EXPECTED" ]; then
    echo "⚠️  WARNING: BSS size is larger than expected"
    echo ""
    echo "Expected at most $((MAX_BSS_EXPECTED / 1024)) KB, got $((BSS_ACTUAL / 1024)) KB"
    echo ""
    echo "This might indicate:"
    echo "  - Additional MEMP pools were added"
    echo "  - Configuration changed without updating this script"
    echo "  - New lwIP structures allocated in BSS"
    echo ""
    echo "Proceeding anyway, but verify this is intentional."
    echo ""
fi

echo "✅ lwIP build validation passed"
echo ""

exit 0
