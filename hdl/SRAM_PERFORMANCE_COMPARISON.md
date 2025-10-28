# SRAM Controller Performance Comparison

**Date**: October 27, 2025
**Purpose**: Compare current 3-layer architecture vs proposed unified controller

---

## SRAM Chip Specifications

**Part**: IS61WV51216BLL-10TLI (512KB, 16-bit wide)

| Parameter | Min | Typical | Max | Units |
|-----------|-----|---------|-----|-------|
| tAA (Address Access Time) | - | - | 10 | ns |
| tWP (Write Pulse Width) | 7 | - | - | ns |
| tWR (Write Recovery) | 0 | - | - | ns |
| tAS (Address Setup) | 0 | - | - | ns |
| tDH (Data Hold) | 0 | - | - | ns |

**Clock**: 50 MHz = 20ns per cycle

---

## Current Implementation (3-Layer Architecture)

### Architecture
```
CPU (32-bit interface)
    ↓
mem_controller.v (routing layer)
    ↓
sram_proc_new.v (32→16-bit converter + strobe handling)
    ↓
sram_driver_new.v (physical SRAM interface)
    ↓
SRAM chip (16-bit)
```

### sram_driver_new.v States (Per 16-bit Access)

| Cycle | State | Actions | Duration |
|-------|-------|---------|----------|
| 0 | IDLE | Wait for valid | Variable |
| 1 | SETUP | Assert addr, CS; setup data if write | 20ns |
| 2 | ACTIVE | Assert WE (write) or sample data (read) | 20ns |
| 3 | RECOVERY | Deassert WE, data hold | 20ns |
| 4 | COOLDOWN | Mandatory gap before next transaction | 20ns |

**Total per 16-bit access: 4 cycles = 80ns**

### sram_proc_new.v States (Per 32-bit Access)

**Full 32-bit Write** (wstrb = 4'b1111):
| Cycle | State | Action |
|-------|-------|--------|
| 0 | IDLE | Wait for start |
| 1 | WRITE_LOW | Assert valid to driver (LOW halfword) |
| 2-4 | - | Driver processes (3 cycles + COOLDOWN) |
| 5 | WRITE_WAIT1 | Wait state (handshaking) |
| 6 | WRITE_SETUP_HIGH | Setup HIGH halfword |
| 7 | WRITE_HIGH | Assert valid to driver (HIGH halfword) |
| 8-10 | - | Driver processes (3 cycles + COOLDOWN) |
| 11 | Done | Assert done signal |

**Total: 11 cycles = 220ns**

**Full 32-bit Read**:
| Cycle | State | Action |
|-------|-------|--------|
| 0 | IDLE | Wait for start |
| 1 | READ_LOW | Assert valid to driver (LOW halfword) |
| 2-4 | - | Driver processes (3 cycles + COOLDOWN) |
| 5 | READ_WAIT1 | Wait state |
| 6 | READ_SETUP_HIGH | Setup HIGH halfword |
| 7 | READ_HIGH | Assert valid to driver (HIGH halfword) |
| 8-10 | - | Driver processes (3 cycles + COOLDOWN) |
| 11 | Done | Assemble 32-bit result |

**Total: 11 cycles = 220ns**

**Partial Write** (e.g., wstrb = 4'b0001 - single byte):
| Cycle | State | Action |
|-------|-------|--------|
| 0 | IDLE | Wait for start |
| 1-11 | READ LOW+HIGH | Read both halfwords (11 cycles) |
| 12 | MODIFY | Merge old data with new byte |
| 13 | WRITE_LOW | Write modified LOW halfword |
| 14-16 | - | Driver processes |
| 17 | WRITE_WAIT1 | Wait state |
| 18 | WRITE_SETUP_HIGH | Setup HIGH halfword |
| 19 | WRITE_HIGH | Write HIGH halfword (if affected) |
| 20-22 | - | Driver processes |
| 23 | Done | Complete |

**Total: 23-28 cycles = 460-560ns** (depends on which halfwords affected)

### Current Performance Summary

| Operation | Cycles | Time @ 50MHz | Notes |
|-----------|--------|--------------|-------|
| 32-bit Read | 11-15 | 220-300ns | ~2.2-3x SRAM spec |
| 32-bit Write (full) | 11-15 | 220-300ns | ~2.2-3x SRAM spec |
| Byte Write (RMW) | 23-29 | 460-580ns | Read + modify + write |
| Halfword Write (RMW) | 23-29 | 460-580ns | Read + modify + write |

**Overhead Sources:**
1. COOLDOWN state (1 cycle per 16-bit = 2 cycles per 32-bit) = **UNNECESSARY**
2. WAIT1/WAIT2 states (handshaking) = 2-3 cycles per 32-bit = **UNNECESSARY**
3. SETUP_HIGH states (extra handshaking) = 1 cycle per 32-bit = **UNNECESSARY**
4. Two-layer state machines = handshaking overhead = **INEFFICIENT**

---

## Proposed Unified Controller

### Architecture
```
CPU (32-bit interface)
    ↓
sram_controller_unified.v (single module)
    ↓
SRAM chip (16-bit)
```

**Key Changes:**
- Single state machine (no handshaking overhead)
- Direct 16-bit SRAM control
- Minimal states per operation
- No unnecessary wait/cooldown states

### State Machine Design

**Full 32-bit Write** (wstrb = 4'b1111):
| Cycle | State | Actions | Timing |
|-------|-------|---------|--------|
| 0 | IDLE | Wait for valid | - |
| 1 | WRITE_LOW_SETUP | Assert addr[0], data[15:0], CS | 20ns |
| 2 | WRITE_LOW_PULSE | Assert WE (pulse = 20ns > 7ns min ✓) | 20ns |
| 3 | WRITE_HIGH_SETUP | Assert addr[1], data[31:16], CS | 20ns |
| 4 | WRITE_HIGH_PULSE | Assert WE, then done | 20ns |

**Total: 4 cycles = 80ns** (was 11-15 cycles)

**Speedup: 2.75-3.75x faster**

**Full 32-bit Read**:
| Cycle | State | Actions | Timing |
|-------|-------|---------|--------|
| 0 | IDLE | Wait for valid | - |
| 1 | READ_LOW_SETUP | Assert addr[0], CS, OE | 20ns setup |
| 2 | READ_LOW_CAPTURE | Sample data[15:0] (tAA = 10ns < 20ns ✓) | 20ns |
| 3 | READ_HIGH_SETUP | Assert addr[1], CS, OE | 20ns setup |
| 4 | READ_HIGH_CAPTURE | Sample data[31:16], done | 20ns |

**Total: 4 cycles = 80ns** (was 11-15 cycles)

**Speedup: 2.75-3.75x faster**

**Byte Write** (wstrb = 4'b0001 - byte 0 only):
| Cycle | State | Actions | Timing |
|-------|-------|---------|--------|
| 0 | IDLE | Wait for valid | - |
| 1 | READ_LOW_SETUP | Assert addr[0], CS, OE (need to read for RMW) | 20ns |
| 2 | READ_LOW_CAPTURE | Sample data[15:0] | 20ns |
| 3 | MODIFY | Merge: keep data[15:8], replace data[7:0] | 0ns (combinational) |
| 4 | WRITE_LOW_SETUP | Assert addr[0], modified_data[15:0], CS | 20ns |
| 5 | WRITE_LOW_PULSE | Assert WE | 20ns |
| 6 | READ_HIGH_SETUP | Assert addr[1], CS, OE | 20ns |
| 7 | READ_HIGH_CAPTURE | Sample data[31:16] (unchanged), done | 20ns |

**Total: 7 cycles = 140ns** (was 23-29 cycles)

**Speedup: 3.3-4.1x faster**

**Halfword Write** (wstrb = 4'b0011 - bytes 0-1, aligned):
| Cycle | State | Actions | Notes |
|-------|-------|---------|-------|
| 0 | IDLE | Wait for valid | - |
| 1 | WRITE_LOW_SETUP | Assert addr[0], data[15:0], CS | Direct write (aligned) |
| 2 | WRITE_LOW_PULSE | Assert WE | No RMW needed! |
| 3 | READ_HIGH_SETUP | Assert addr[1], CS, OE | Unchanged half |
| 4 | READ_HIGH_CAPTURE | Sample data[31:16], done | Complete |

**Total: 4 cycles = 80ns** (was 23-29 cycles)

**Speedup: 5.75-7.25x faster** (aligned halfword doesn't need RMW!)

---

## Performance Comparison Summary

| Operation | Current | Proposed | Speedup | Notes |
|-----------|---------|----------|---------|-------|
| **32-bit Read** | 11-15 cycles | **4 cycles** | **2.75-3.75x** | Eliminate waits |
| **32-bit Write** | 11-15 cycles | **4 cycles** | **2.75-3.75x** | Eliminate waits |
| **Byte Write (RMW)** | 23-29 cycles | **7 cycles** | **3.3-4.1x** | Read+modify+write |
| **Halfword Write (aligned)** | 23-29 cycles | **4 cycles** | **5.75-7.25x** | No RMW needed! |
| **Halfword Write (unaligned)** | 23-29 cycles | **7-9 cycles** | **2.6-4.1x** | RMW on both halves |

### Time Comparison @ 50MHz

| Operation | Current (ns) | Proposed (ns) | Savings (ns) |
|-----------|--------------|---------------|--------------|
| 32-bit Read | 220-300 | **80** | 140-220 |
| 32-bit Write | 220-300 | **80** | 140-220 |
| Byte Write | 460-580 | **140** | 320-440 |
| Halfword Write (aligned) | 460-580 | **80** | 380-500 |

---

## Theoretical vs Actual Limits

### SRAM Chip Limits (10ns access time)

**Theoretical minimum @ 50MHz (20ns/cycle):**
- 16-bit access: 1 cycle (but need setup + capture = 2 cycles practical)
- 32-bit access: 2 cycles (two 16-bit accesses, pipelined perfectly)

**Our proposed design:**
- 32-bit access: 4 cycles = **2x chip theoretical minimum**

**Why not 2 cycles?**
- Need 1 cycle for address setup before sampling read data (tAA = 10ns)
- Need 1 cycle for WE pulse (tWP = 7ns min, we provide 20ns)
- Can't pipeline because 32-bit = two sequential 16-bit accesses to different addresses

**We're hitting the practical limit given:**
1. 50MHz clock (20ns cycle)
2. 16-bit wide SRAM (need 2 accesses per 32-bit)
3. Non-pipelined architecture (simpler, safer)

---

## Expected System Impact

### CPU Performance
- **Instruction fetch**: 2.75-3.75x faster (fewer wait states)
- **Data loads**: 2.75-3.75x faster
- **Data stores**: 2.75-3.75x faster (full word), 3-7x faster (partial)
- **Overall**: ~2-3x effective CPU performance boost

### Application Benefits
- **Code execution**: Faster instruction fetch
- **Graphics/ncurses**: Faster screen buffer updates
- **Overlays**: Faster loading from SD card to SRAM
- **General computation**: Fewer memory wait states

---

## Risk Assessment

### Low Risk Changes
✅ Eliminating COOLDOWN state (tWR = 0ns, no recovery needed)
✅ Eliminating WAIT1/WAIT2 states (no timing constraint requires them)
✅ Eliminating SETUP_HIGH state (redundant handshaking)

### Medium Risk Changes
⚠️ 2-cycle 16-bit access instead of 4-cycle
- Still meets all timing specs (tAA, tWP, tWR)
- 20ns per cycle >> 10ns access time
- Should work, but needs hardware validation

### Mitigation
✅ We have baseline test suite (8 tests, 100% passing)
✅ We have git tag for instant revert (v0.12-baseline-tests)
✅ Same test suite will validate optimized version

---

## Implementation Plan

1. **Design**: Create sram_controller_unified.v (single module)
2. **Simulate**: Testbench with timing checks
3. **Integrate**: Modify ice40_picorv32_top.v to use new controller
4. **Build**: Synthesize new bitstream
5. **Test**: Run memory_test_baseline_safe.bin
6. **Validate**: Must achieve 100% pass rate
7. **Measure**: Compare actual vs expected performance
8. **Revert if needed**: git reset --hard v0.12-baseline-tests

---

## Success Criteria

**Must Pass:**
- ✅ All 8 baseline tests (100% success rate)
- ✅ No timing violations in synthesis
- ✅ No setup/hold violations

**Performance Goals:**
- 🎯 32-bit read/write: 4 cycles (target) vs 11-15 cycles (current)
- 🎯 Byte write: 7 cycles (target) vs 23-29 cycles (current)

**If any test fails:**
- Revert immediately to v0.12-baseline-tests
- Analyze failure
- Adjust design
- Re-test

---

## Conclusion

The proposed unified SRAM controller achieves:
- **2.75-7.25x speedup** across all operation types
- **Simpler design** (single module vs 3 layers)
- **Meets all SRAM timing specs** with margin
- **Low risk** (validated test suite + easy revert)

Expected impact: **~2-3x overall system performance improvement**

Ready to implement!

---

**Author**: Michael Wolak (mikewolak@gmail.com)
**Date**: October 27, 2025
