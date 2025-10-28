#!/bin/bash
#==============================================================================
# Memory Test Sequence - Progressive Debugging
#==============================================================================

UPLOADER="../tools/uploader/fw_upload_fast"

echo "================================================================================"
echo "Memory Test Debugging Sequence"
echo "================================================================================"
echo ""
echo "This script will guide you through testing the memory test binaries"
echo "in progressive order to isolate the lockup issue."
echo ""
echo "--------------------------------------------------------------------------------"
echo "Test 1: memory_test_minimal.bin (ALREADY CONFIRMED WORKING)"
echo "--------------------------------------------------------------------------------"
echo ""
echo "This test just prints 2 messages. User confirmed this works."
echo "Skipping..."
echo ""

echo "--------------------------------------------------------------------------------"
echo "Test 2: memory_test_simple.bin"
echo "--------------------------------------------------------------------------------"
echo ""
echo "What it tests:"
echo "  - Printf (already proven)"
echo "  - Single memory write/read at 0x00020000"
echo "  - Basic pointer arithmetic"
echo ""
echo "Expected output:"
echo "  ========================================"
echo "  Simple Memory Test"
echo "  ========================================"
echo "  Test 1: Printf works!"
echo "  Test 2: x = 42"
echo "  Test 3: ptr = 0x000XXXXX, *ptr = 42"
echo "  Test 4: Wrote 0xDEADBEEF, read 0xDEADBEEF"
echo "  SUCCESS: All tests passed!"
echo "  Done. Looping forever..."
echo ""
read -p "Press ENTER to upload memory_test_simple.bin..."
$UPLOADER memory_test_simple.bin
echo ""
echo "Did you see the expected output? (y/n)"
read -r RESPONSE
if [ "$RESPONSE" != "y" ]; then
    echo ""
    echo "ERROR: memory_test_simple.bin failed!"
    echo "This means even a single memory access at 0x00020000 causes issues."
    echo "Likely causes:"
    echo "  - Memory map problem"
    echo "  - SRAM addressing bug"
    echo "  - Heap/stack collision"
    echo ""
    echo "DO NOT PROCEED to Test 3."
    echo "Review memory map and linker script."
    exit 1
fi

echo ""
echo "Test 2 PASSED - Single memory access works!"
echo ""

echo "--------------------------------------------------------------------------------"
echo "Test 3: memory_test_debug.bin"
echo "--------------------------------------------------------------------------------"
echo ""
echo "What it tests (10 progressive stages):"
echo "  Test 1: Printf"
echo "  Test 2: Local variable"
echo "  Test 3: Create pointer to TEST_BASE (0x00010000)"
echo "  Test 4: Single write to TEST_BASE"
echo "  Test 5: Single read from TEST_BASE"
echo "  Test 6: Loop 10 writes"
echo "  Test 7: Loop 10 reads"
echo "  Test 8: Loop 100 writes"
echo "  Test 9: Loop 100 reads"
echo "  Test 10: Full 4KB writes (1024 words)"
echo ""
echo "This will show EXACTLY which test causes the lockup."
echo ""
read -p "Press ENTER to upload memory_test_debug.bin..."
$UPLOADER memory_test_debug.bin
echo ""
echo "How far did it get before locking up?"
echo "1) All 10 tests passed"
echo "2) Locked up at test 3-5 (pointer/single access)"
echo "3) Locked up at test 6-7 (small loops)"
echo "4) Locked up at test 8-9 (medium loops)"
echo "5) Locked up at test 10 (full 4KB)"
read -r TEST_RESULT

case $TEST_RESULT in
    1)
        echo ""
        echo "All tests passed! Problem is in baseline test logic itself."
        echo "The issue is not with memory access patterns."
        echo "Check:"
        echo "  - Test macros (TEST_START, TEST_PASS, ASSERT)"
        echo "  - Printf format strings"
        echo "  - Global test counters"
        ;;
    2)
        echo ""
        echo "Locked at pointer creation or single access at TEST_BASE."
        echo "Issue: TEST_BASE address (0x00010000) may be problematic."
        echo "Binary is ~40KB, TEST_BASE at 64KB should be safe."
        echo "Check:"
        echo "  - Actual binary size (may be larger than reported)"
        echo "  - Memory map overlaps"
        echo "  - SRAM controller address decoding"
        ;;
    3)
        echo ""
        echo "Locked at small loops (10 iterations)."
        echo "Issue: Loop overhead or printf in loops."
        echo "Check:"
        echo "  - Stack usage"
        echo "  - Printf buffer issues"
        echo "  - Optimize loop code"
        ;;
    4)
        echo ""
        echo "Locked at medium loops (100 iterations)."
        echo "Issue: Iteration count threshold or cumulative effect."
        echo "Check:"
        echo "  - UART FIFO overflow"
        echo "  - Stack usage accumulation"
        echo "  - Loop iteration limits"
        ;;
    5)
        echo ""
        echo "Locked at full 4KB writes (1024 words)."
        echo "Issue: Large memory access or address boundary crossing."
        echo "Check:"
        echo "  - TEST_BASE + 4KB crosses 68KB boundary"
        echo "  - SRAM controller state machine limits"
        echo "  - Stack overflow from large loop"
        ;;
    *)
        echo ""
        echo "Invalid input. Please re-run and note which test fails."
        exit 1
        ;;
esac

echo ""
echo "================================================================================"
echo "Debugging Complete"
echo "================================================================================"
echo ""
echo "Next steps:"
echo "1. Review the analysis above"
echo "2. Fix memory_test_baseline.c based on findings"
echo "3. Rebuild: make TARGET=memory_test_baseline USE_NEWLIB=1 single-target"
echo "4. Re-test baseline"
echo "5. Proceed to SRAM optimization Phase 2 (hardware testing)"
echo ""
