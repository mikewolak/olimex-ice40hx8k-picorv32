#!/bin/bash
#===============================================================================
# FreeRTOS Build Validation Script
# Verifies that the compiled binary matches the configuration settings
#
# Usage: validate_freertos_build.sh <elf_file> <config_file>
#
# Exit codes:
#   0 = All checks passed
#   1 = Validation failed
#===============================================================================

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <elf_file> <config_file>"
    exit 1
fi

ELF_FILE="$1"
CONFIG_FILE="$2"

if [ ! -f "$ELF_FILE" ]; then
    echo "ERROR: ELF file not found: $ELF_FILE"
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: Config file not found: $CONFIG_FILE"
    exit 1
fi

# Source the config file
. "$CONFIG_FILE"

echo "========================================="
echo "FreeRTOS Build Validation"
echo "========================================="
echo "ELF File: $ELF_FILE"
echo "Config:   $CONFIG_FILE"
echo ""

FAILED=0

# Check 1: Verify ucHeap size matches CONFIG_FREERTOS_TOTAL_HEAP_SIZE
echo "Checking FreeRTOS heap size..."
EXPECTED_HEAP=$CONFIG_FREERTOS_TOTAL_HEAP_SIZE
ACTUAL_HEAP_HEX=$(riscv64-unknown-elf-objdump -t "$ELF_FILE" | grep "ucHeap$" | awk '{print $5}')

if [ -z "$ACTUAL_HEAP_HEX" ]; then
    echo "  ✗ FAILED: ucHeap symbol not found in binary!"
    FAILED=1
else
    ACTUAL_HEAP=$((0x$ACTUAL_HEAP_HEX))

    if [ "$ACTUAL_HEAP" -eq "$EXPECTED_HEAP" ]; then
        echo "  ✓ PASSED: Heap size = $ACTUAL_HEAP bytes (0x$ACTUAL_HEAP_HEX)"
    else
        echo "  ✗ FAILED: Heap size mismatch!"
        echo "    Expected: $EXPECTED_HEAP bytes (0x$(printf '%x' $EXPECTED_HEAP))"
        echo "    Actual:   $ACTUAL_HEAP bytes (0x$ACTUAL_HEAP_HEX)"
        FAILED=1
    fi
fi

# Check 2: Verify all required FreeRTOS symbols are present
echo ""
echo "Checking required FreeRTOS symbols..."
REQUIRED_SYMBOLS=(
    "xTaskCreate"
    "vTaskStartScheduler"
    "vTaskDelay"
    "xTaskGetTickCount"
    "uxTaskGetNumberOfTasks"
    "xPortGetFreeHeapSize"
    "xPortGetMinimumEverFreeHeapSize"
    "vApplicationIdleHook"
)

for SYM in "${REQUIRED_SYMBOLS[@]}"; do
    if riscv64-unknown-elf-nm "$ELF_FILE" | grep -q " T $SYM\$"; then
        echo "  ✓ Found: $SYM"
    else
        echo "  ✗ FAILED: Missing symbol: $SYM"
        FAILED=1
    fi
done

# Check 3: Verify no undefined symbols
echo ""
echo "Checking for undefined symbols..."
UNDEFINED=$(riscv64-unknown-elf-nm "$ELF_FILE" | grep " U " | wc -l)
if [ "$UNDEFINED" -eq 0 ]; then
    echo "  ✓ PASSED: No undefined symbols"
else
    echo "  ✗ FAILED: Found $UNDEFINED undefined symbols:"
    riscv64-unknown-elf-nm "$ELF_FILE" | grep " U " | head -10
    FAILED=1
fi

# Check 4: Verify memory layout is sane
echo ""
echo "Checking memory layout..."
SIZE_OUTPUT=$(riscv64-unknown-elf-size "$ELF_FILE")
TEXT=$(echo "$SIZE_OUTPUT" | tail -1 | awk '{print $1}')
DATA=$(echo "$SIZE_OUTPUT" | tail -1 | awk '{print $2}')
BSS=$(echo "$SIZE_OUTPUT" | tail -1 | awk '{print $3}')
TOTAL=$((TEXT + DATA + BSS))

echo "  Text:  $TEXT bytes"
echo "  Data:  $DATA bytes"
echo "  BSS:   $BSS bytes"
echo "  Total: $TOTAL bytes"

# Check that BSS contains at least the heap
MIN_BSS=$((CONFIG_FREERTOS_TOTAL_HEAP_SIZE))
if [ "$BSS" -ge "$MIN_BSS" ]; then
    echo "  ✓ PASSED: BSS ($BSS) >= heap size ($MIN_BSS)"
else
    echo "  ✗ FAILED: BSS ($BSS) < heap size ($MIN_BSS)"
    FAILED=1
fi

# Check total doesn't exceed application SRAM (256KB = 262144 bytes)
MAX_SIZE=262144
if [ "$TOTAL" -le "$MAX_SIZE" ]; then
    echo "  ✓ PASSED: Total size ($TOTAL) fits in application SRAM ($MAX_SIZE)"
else
    echo "  ✗ FAILED: Total size ($TOTAL) exceeds application SRAM ($MAX_SIZE)"
    FAILED=1
fi

# Check 5: Verify critical task symbols exist
echo ""
echo "Checking task function symbols..."
TASK_SYMBOLS=(
    "vTask1_Counter"
    "vTask2_FloatDemo"
    "vTask3_SystemStatus"
)

for SYM in "${TASK_SYMBOLS[@]}"; do
    if riscv64-unknown-elf-nm "$ELF_FILE" | grep -q " [Tt] $SYM\$"; then
        echo "  ✓ Found: $SYM"
    else
        echo "  ⚠ WARNING: Task symbol not found: $SYM (may not be used in this firmware)"
    fi
done

# Final result
echo ""
echo "========================================="
if [ "$FAILED" -eq 0 ]; then
    echo "✓ ALL VALIDATION CHECKS PASSED"
    echo "========================================="
    exit 0
else
    echo "✗ VALIDATION FAILED"
    echo "========================================="
    exit 1
fi
