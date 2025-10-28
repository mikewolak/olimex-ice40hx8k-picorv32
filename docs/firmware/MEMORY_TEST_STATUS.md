# Memory Test Suite - Debugging Status

**Date**: October 27, 2025
**Status**: Incremental testing to isolate lockup issue

---

## Current Situation

**Problem**: `memory_test_baseline.bin` locks up on hardware
**Root Cause Fixed**: rdcycle instruction removed (PicoRV32 has ENABLE_COUNTERS=0)
**Remaining Issue**: Still locks up even after rdcycle fix

**Working Confirmation**: `memory_test_minimal.bin` WORKS - proves newlib/printf/start.S are OK

---

## Test Binaries (Progressive Complexity)

All binaries successfully built and ready for testing:

### 1. memory_test_minimal.bin (40KB) ✅ WORKS
**Status**: User confirmed working
**What it does**:
- Prints 2 simple messages
- Infinite loop with WFI
- NO memory access beyond stack

**Expected output**:
```
Hello from memory test!
If you see this, printf works.
```

**Conclusion**: Basic system (newlib, printf, UART, start.S) is functional

---

### 2. memory_test_simple.bin (40KB) - NEEDS TESTING
**Status**: Built, ready to test
**What it does**:
- Printf works (proven by test 1)
- Single memory write/read at 0x00020000 (128KB offset)
- Writes: 0xDEADBEEF
- Reads back and verifies

**Expected output**:
```
========================================
Simple Memory Test
========================================

Test 1: Printf works!
Test 2: x = 42
Test 3: ptr = 0x000XXXXX, *ptr = 42
Test 4: Wrote 0xDEADBEEF, read 0xDEADBEEF

SUCCESS: All tests passed!

Done. Looping forever...
```

**Purpose**:
- Isolates whether SINGLE memory access at high address causes issue
- Tests pointer arithmetic
- Tests volatile access pattern

**Upload command**:
```bash
cd tools/uploader
./fw_upload_fast ../../firmware/memory_test_simple.bin
```

---

### 3. memory_test_debug.bin (41KB) - NEEDS TESTING
**Status**: Built, ready to test
**What it does**: Runs 10 progressive tests, printing "OK" after each:

1. Printf (already proven working)
2. Local variable (stack access)
3. Create pointer to TEST_BASE (0x00010000)
4. Single write to TEST_BASE
5. Single read from TEST_BASE
6. Loop 10 writes
7. Loop 10 reads
8. Loop 100 writes
9. Loop 100 reads
10. Full 4KB writes (1024 words)

**Expected output**:
```
Memory Test - Debug Version
Testing each component separately...

Test 1: Printf... OK
Test 2: Local variable... OK (value=0x12345678)
Test 3: Create pointer to TEST_BASE... OK (ptr=0x00010000)
Test 4: Single write to TEST_BASE... OK
Test 5: Single read from TEST_BASE... OK (val=0xDEADBEEF)
Test 6: Loop 10 writes... OK
Test 7: Loop 10 reads... OK
Test 8: Loop 100 writes... OK
Test 9: Loop 100 reads... OK
Test 10: Full 4KB writes... OK

ALL DEBUG TESTS PASSED!
```

**Purpose**:
- Incrementally increases complexity
- Shows EXACTLY which operation causes lockup
- If locks up at Test 6, we know loops of 10 are the problem
- If locks up at Test 10, we know large 4KB access is the problem

**Upload command**:
```bash
cd tools/uploader
./fw_upload_fast ../../firmware/memory_test_debug.bin
```

---

### 4. memory_test_baseline.bin (45KB) ❌ LOCKS UP
**Status**: Known to lock up (even after rdcycle fix)
**What it does**: Full comprehensive test suite with 10 test cases

**Problem**: Too complex - can't isolate which specific test causes lockup

**DO NOT TEST** until tests 2 and 3 pass and isolate the issue.

---

## Testing Strategy

### Step 1: Test memory_test_simple.bin
This will answer: "Does a single memory access at 0x00020000 work?"

**Possible outcomes**:
- **Works**: Single access OK, problem is in loops or patterns → proceed to Step 2
- **Locks up**: Even single access fails at high address → memory map issue

### Step 2: Test memory_test_debug.bin
This will answer: "Which specific operation causes the lockup?"

**Possible outcomes**:
- **All pass**: Problem is in baseline test logic itself
- **Locks at Test 3-5**: Pointer creation or TEST_BASE address issue
- **Locks at Test 6-7**: Small loops (10 iterations) are problematic
- **Locks at Test 8-9**: Medium loops (100 iterations) hit a limit
- **Locks at Test 10**: Full 4KB access exceeds some boundary

### Step 3: Analyze and Fix
Based on which test fails, we'll know:
- Memory addressing issue
- Loop iteration limit
- Stack overflow from recursion
- Address boundary crossing bug
- SRAM controller state machine issue

### Step 4: Fix memory_test_baseline.bin
Once root cause identified, fix the baseline test and verify 100% pass rate.

---

## Known Good Memory Map

From linker.ld and actual builds:

```
0x00000000 - 0x00009B2F   Code (.text)     - ~39KB for minimal test
0x00009B30 - 0x00009CFF   Data (.data)     - ~460 bytes
0x00009D00 - 0x00009E4F   BSS (.bss)       - ~336 bytes
0x00009E50 - 0x00041FFF   Free space       - ~230KB
0x00042000 - 0x0007FFFF   Heap             - 248KB (managed by newlib)
0x00080000                Stack (top)      - grows down

Test regions:
0x00010000 (TEST_BASE)    - 64KB offset (safe - code is only ~40KB)
0x00020000 (SIMPLE_TEST)  - 128KB offset (very safe)
```

**Conclusion**: Both test regions are well beyond code/data sections and should be safe.

---

## Detailed Test Results (To Be Filled In)

### memory_test_minimal.bin
- **Upload timestamp**: Earlier session
- **Result**: ✅ WORKS
- **Output received**: Yes - printf messages confirmed
- **Conclusion**: Basic system functional

### memory_test_simple.bin
- **Upload timestamp**: _Pending_
- **Result**: _To be tested_
- **Output received**: _Pending_
- **Conclusion**: _Pending_

### memory_test_debug.bin
- **Upload timestamp**: _Pending_
- **Result**: _To be tested_
- **Last successful test**: _Pending_
- **Lockup at test**: _Pending_
- **Conclusion**: _Pending_

### memory_test_baseline.bin
- **Upload timestamp**: Earlier session
- **Result**: ❌ LOCKS UP (screen clears, no output)
- **Output received**: None - immediate lockup
- **Conclusion**: DO NOT RE-TEST until issue isolated by tests 2 & 3

---

## Next Steps

1. **Upload and test memory_test_simple.bin** first
2. **Upload and test memory_test_debug.bin** second
3. **Analyze results** and identify root cause
4. **Fix memory_test_baseline.c** based on findings
5. **Rebuild and re-test baseline** to confirm fix
6. **Proceed to Phase 2** of SRAM optimization (hardware testing)

---

## Files in This Test Suite

1. `memory_test_minimal.c` - Minimal printf test (18 lines)
2. `memory_test_simple.c` - Single memory access (47 lines)
3. `memory_test_debug.c` - Incremental 10-stage test (87 lines)
4. `memory_test_baseline.c` - Full test suite (440 lines)
5. `MEMORY_TEST_STATUS.md` - This file

---

## Build Commands (All Already Built)

```bash
# Already completed:
make TARGET=memory_test_minimal USE_NEWLIB=1 single-target
make TARGET=memory_test_simple USE_NEWLIB=1 single-target
make TARGET=memory_test_debug USE_NEWLIB=1 single-target
make TARGET=memory_test_baseline USE_NEWLIB=1 single-target

# All binaries ready in firmware/ directory
```

---

## Upload Commands

```bash
cd tools/uploader

# Test 1: Already confirmed working
./fw_upload_fast ../../firmware/memory_test_minimal.bin

# Test 2: Next to test
./fw_upload_fast ../../firmware/memory_test_simple.bin

# Test 3: After test 2
./fw_upload_fast ../../firmware/memory_test_debug.bin

# Test 4: Only after issue isolated and fixed
./fw_upload_fast ../../firmware/memory_test_baseline.bin
```

---

## Expected Timeline

- **Test 2**: 5 minutes (upload + verify output)
- **Test 3**: 5 minutes (upload + note which test fails)
- **Analysis**: 15 minutes (understand root cause)
- **Fix**: 30 minutes (modify code, rebuild, re-test)
- **Verification**: 10 minutes (final baseline test)

**Total**: ~1 hour to complete debugging and establish baseline

---

## Critical Notes

1. **rdcycle bug already fixed** - All binaries rebuilt without rdcycle
2. **memory_test_minimal.bin works** - Proves basic system OK
3. **Incremental approach** - Each test adds one more complexity level
4. **DO NOT modify HDL** - Until 100% test pass rate achieved
5. **Test-first methodology** - Baseline must pass before optimization

---

## Author

Michael Wolak (mikewolak@gmail.com)
October 2025
