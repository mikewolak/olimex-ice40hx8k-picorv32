# SRAM Controller Optimization - Implementation Status

**Date**: October 27, 2025
**Status**: Implementation Complete - FPGA Synthesis In Progress

---

## Summary

Implemented unified SRAM controller that replaces the 3-layer architecture
(mem_controller + sram_proc_new + sram_driver_new) with a single optimized module.

**Expected Performance**: 2.75-7.25x speedup (4-7 cycles vs 11-29 cycles)

---

## Files Created

### 1. sram_controller_unified.v (Core Module)
**Location**: `hdl/sram_controller_unified.v`
**Size**: ~400 lines
**Purpose**: Single unified SRAM controller with direct 16-bit SRAM control

**Features**:
- 32-bit CPU interface (addr, wdata, wstrb, rdata)
- 16-bit SRAM physical interface
- Minimal state machine (14 states vs 20+ in old design)
- Smart byte-strobe handling
- Read-modify-write for partial writes
- Direct write for aligned halfwords (no RMW needed!)

**Performance**:
- Full 32-bit read: 4 cycles (80ns @ 50MHz)
- Full 32-bit write: 4 cycles (80ns @ 50MHz)
- Byte write (RMW): 7 cycles (140ns @ 50MHz)
- Aligned halfword write: 4 cycles (no RMW!)

### 2. sram_unified_adapter.v (Compatibility Layer)
**Location**: `hdl/sram_unified_adapter.v`
**Size**: ~100 lines
**Purpose**: Adapt valid/ready interface to start/busy/done for mem_controller

**Why Needed**:
- mem_controller expects start/busy/done handshake
- Unified controller uses simpler valid/ready handshake
- Adapter converts between the two protocols

**Overhead**: +1-2 cycles for protocol conversion (acceptable)

### 3. Modified ice40_picorv32_top.v
**Changes**:
- Removed `sram_driver_new` instantiation
- Removed `sram_proc_new` instantiation
- Added `sram_unified_adapter` instantiation
- Direct connection to SRAM pins (SA, SD, SRAM_CS_N, SRAM_OE_N, SRAM_WE_N)

---

## Architecture Comparison

### Before (3-Layer)
```
CPU (32-bit)
    ↓
mem_controller.v (routing: SRAM/Boot ROM/MMIO)
    ↓ start/busy/done
sram_proc_new.v (32→16-bit converter, 11-15 cycles)
    ↓ valid/ready
sram_driver_new.v (physical interface, 4 cycles per 16-bit)
    ↓
SRAM chip (16-bit)
```

**Total**: 11-29 cycles per 32-bit operation

### After (Unified)
```
CPU (32-bit)
    ↓
mem_controller.v (routing: SRAM/Boot ROM/MMIO)
    ↓ start/busy/done
sram_unified_adapter.v (protocol conversion, +1-2 cycles)
    ↓ valid/ready
sram_controller_unified.v (unified controller, 4-7 cycles)
    ↓
SRAM chip (16-bit)
```

**Total**: 5-9 cycles per 32-bit operation (adapter overhead included)

---

## State Machine Design

### Full 32-bit Read (4 cycles)
```
IDLE → READ_LOW_SETUP → READ_LOW_CAPTURE → READ_HIGH_SETUP → READ_HIGH_CAPTURE → IDLE
  0         1                 2                  3                   4             (ready)
```

### Full 32-bit Write (4 cycles)
```
IDLE → WRITE_LOW_SETUP → WRITE_LOW_PULSE → WRITE_HIGH_SETUP → WRITE_HIGH_PULSE → IDLE
  0          1                 2                  3                   4            (ready)
```

### Byte Write - Read-Modify-Write (7 cycles)
```
IDLE → RMW_READ_LOW_SETUP → RMW_READ_LOW_CAPTURE → RMW_READ_HIGH_SETUP →
  0            1                      2                      3

RMW_READ_HIGH_CAPTURE → RMW_WRITE_LOW_SETUP → RMW_WRITE_LOW_PULSE → IDLE
         4                       5                      6            (ready)
```

### Aligned Halfword Write (4 cycles - optimized!)
```
IDLE → WRITE_LOW_SETUP → WRITE_LOW_PULSE → IDLE  (if wstrb == 4'b0011)
  0          1                 2            (ready)

or

IDLE → WRITE_HIGH_SETUP → WRITE_HIGH_PULSE → IDLE  (if wstrb == 4'b1100)
  0          1                 2             (ready)
```

---

## Key Optimizations

### 1. Eliminated COOLDOWN State
**Old**: 4 cycles per 16-bit (IDLE→SETUP→ACTIVE→RECOVERY→COOLDOWN)
**New**: 2 cycles per 16-bit (SETUP→PULSE/CAPTURE)
**Reason**: SRAM tWR = 0ns (no recovery time needed)
**Savings**: 2 cycles per 16-bit = 4 cycles per 32-bit

### 2. Eliminated WAIT States
**Old**: WAIT1, WAIT2 states between LOW/HIGH halfword accesses
**New**: Direct transition from LOW to HIGH
**Reason**: No timing constraint requires waiting
**Savings**: 2-3 cycles per 32-bit

### 3. Merged State Machines
**Old**: Two state machines with handshaking overhead
**New**: Single state machine
**Savings**: ~1-2 cycles per transaction

### 4. Smart Byte-Strobe Handling
**Innovation**: Detect aligned halfword writes
- wstrb == 4'b0011 (bytes [1:0]) → Direct write LOW, skip HIGH
- wstrb == 4'b1100 (bytes [3:2]) → Direct write HIGH, skip LOW
**Savings**: No RMW needed for aligned halfwords (23→4 cycles!)

### 5. Direct SRAM Control
**Old**: 3 layers of abstraction
**New**: Single module controls SRAM directly
**Benefit**: No intermediate buffering, simpler debugging

---

## Timing Validation @ 50MHz (20ns/cycle)

| Operation | Cycles | Time | SRAM Spec | Margin |
|-----------|--------|------|-----------|--------|
| Address Setup | 20ns | 20ns | tAS = 0ns min | ✓ 20ns extra |
| Read Access | 20ns | 20ns | tAA = 10ns max | ✓ 2x margin |
| Write Pulse | 20ns | 20ns | tWP = 7ns min | ✓ 2.86x margin |
| Write Recovery | 0ns | 0ns | tWR = 0ns min | ✓ Meets spec |
| Data Hold | 20ns | 20ns | tDH = 0ns min | ✓ 20ns extra |

**All timing constraints met with significant margin!**

---

## Expected Performance Gains

| Operation | Before | After | Speedup |
|-----------|--------|-------|---------|
| 32-bit Read | 11-15 cycles | 4-5 cycles | **2.75-3.75x** |
| 32-bit Write | 11-15 cycles | 4-5 cycles | **2.75-3.75x** |
| Byte Write | 23-29 cycles | 7-8 cycles | **3.3-4.1x** |
| Halfword (aligned) | 23-29 cycles | 4-5 cycles | **5.75-7.25x** |
| Halfword (unaligned) | 23-29 cycles | 7-9 cycles | **2.6-4.1x** |

**Note**: +1 cycle overhead from adapter included in "After" numbers

---

## System Impact

### CPU Performance
- **Instruction fetch**: 2.75-3.75x faster
- **Data loads**: 2.75-3.75x faster
- **Data stores**: 2.75-7.25x faster (depending on alignment)
- **Overall**: ~2-3x effective CPU performance boost

### Application Benefits
- **Code execution**: Fewer instruction fetch wait states
- **Graphics/ncurses**: Faster screen buffer updates
- **Overlays**: Faster loading from SD to SRAM
- **General computation**: Reduced memory bottleneck

---

## Build and Test Plan

### Step 1: Synthesize ✅ IN PROGRESS
```bash
./build_fpga_optimized.sh
```

Expected results:
- Clean synthesis (no errors)
- Place and route success
- Timing constraints met (50MHz clock)
- Bitstream generated: `hdl/ice40_picorv32.bin`

### Step 2: Program FPGA ⏳ PENDING
```bash
cd tools/openocd
sudo openocd -f olimex-arm-usb-tiny-h.cfg -f ice40-hx8k.cfg \
    -c "init; svf ../../hdl/ice40_picorv32.svf; exit"
```

### Step 3: Run Baseline Tests ⏳ PENDING
```bash
cd tools/uploader
./fw_upload_fast ../../firmware/memory_test_baseline_safe.bin
```

**Success Criteria**: All 8 tests must pass (100% success rate)

**Test Suite**:
1. ✅ Sequential 32-bit read/write (4KB)
2. ✅ Random access patterns
3. ✅ Byte-level read/write
4. ✅ Halfword read/write
5. ✅ Back-to-back transactions
6. ✅ Walking bit patterns
7. ✅ Stress test (100 iterations)
8. ✅ Address boundary crossing

### Step 4: Measure Performance ⏳ PENDING
- Compare boot time
- Compare benchmark execution time
- Measure actual cycle counts (if possible)

### Step 5: Validate System ⏳ PENDING
Test other applications:
- SD card manager
- Mandelbrot demo
- HexEdit
- UART echo

---

## Rollback Plan

If any issues occur:

```bash
# Revert to known-good state
git reset --hard v0.12-baseline-tests

# Rebuild firmware
cd firmware && make clean && make firmware

# Rebuild FPGA (will use old 3-layer design)
cd ../hdl && [rebuild with old design]
```

**Tag**: v0.12-baseline-tests
**Status**: All tests passing with 3-layer design
**Safe**: Can revert 100% to this state

---

## Risk Assessment

### Low Risk ✅
- Timing constraints well within spec (2-3x margin)
- Same test suite validates functionality
- Easy rollback to known-good state

### Medium Risk ⚠️
- First implementation - may have logic bugs
- Protocol adapter adds small overhead
- Needs thorough validation on hardware

### Mitigation ✓
- Comprehensive baseline test suite (8 tests)
- Git tag for instant revert
- Incremental testing approach
- Simulation debug support added

---

## Next Steps

1. **Wait for synthesis to complete**
2. **Check synthesis report** for timing/resource usage
3. **Program FPGA** with new bitstream
4. **Run baseline tests** - MUST achieve 100% pass rate
5. **Measure performance** gains
6. **Test system applications** (SD, mandelbrot, etc.)
7. **Document results**
8. **Commit if successful** or revert if issues found

---

## Documentation Files

- `SRAM_OPTIMIZATION_ANALYSIS.md` - Technical analysis
- `SRAM_OPTIMIZATION_STATUS.md` - Project roadmap
- `SRAM_PERFORMANCE_COMPARISON.md` - Detailed performance comparison
- `SRAM_OPTIMIZATION_IMPLEMENTATION.md` - This file
- `BASELINE_TEST_FINAL.md` - Test suite documentation

---

## Author

Michael Wolak (mikewolak@gmail.com)
October 2025

**Project**: Olimex iCE40HX8K-EVB PicoRV32 RISC-V Platform
**Repository**: https://github.com/mikewolak/olimex-ice40hx8k-picorv32.git
