# SRAM Unified Controller - Bug Fix Report

## Date: October 28, 2025

## Summary

Fixed critical valid/ready handshake bug in `sram_controller_unified.v` that caused each memory transaction to execute twice, preventing the PicoRV32 CPU from functioning correctly on the FPGA.

## Problem Description

When the unified SRAM controller was deployed to the FPGA, the PicoRV32 CPU failed to execute any code. The system appeared completely non-functional.

## Root Cause Analysis

### Issue: Double-Transaction Bug

The controller was processing each memory request **twice** due to a timing hazard in the valid/ready handshake protocol.

### Timing Analysis

**Cycle N**: Controller in READ_HIGH_CAPTURE state
- `ready <= 1'b1` (asserted for completion)
- `state <= IDLE` (return to idle state)

**Cycle N+1**: Controller in IDLE state
- `ready = 1` (from previous cycle's non-blocking assignment)
- Adapter still has `valid = 1` (hasn't cleared yet)
- **BUG**: IDLE state checks `if (valid)` and immediately starts ANOTHER transaction!
- Later in same cycle: `ready <= 1'b0` (clears ready)

**Cycle N+2**:
- Adapter finally clears `valid = 0`
- But second (duplicate) transaction has already started

### Simulation Evidence

**Before Fix:**
```
[SRAM_UNIFIED] START: addr=0x00042000 wdata=0xxxxxxxxx wstrb=0x0
[SRAM_MODEL] READ  addr=0x21000 data=0x0000 @ 414805
[SRAM_MODEL] READ  addr=0x21001 data=0x0000 @ 414845
[SRAM_UNIFIED] START: addr=0x00042000 wdata=0xxxxxxxxx wstrb=0x0  ← DUPLICATE!
[SRAM_UNIFIED] COMPLETE READ: addr=0x00042000 rdata=0x00000000
[SRAM_MODEL] READ  addr=0x21000 data=0x0000 @ 414905  ← DUPLICATE READ!
[SRAM_MODEL] READ  addr=0x21001 data=0x0000 @ 414945  ← DUPLICATE READ!
[SRAM_UNIFIED] COMPLETE READ: addr=0x00042000 rdata=0x00000000
```

Each address was being accessed twice, doubling the memory access time and causing synchronization issues with the CPU pipeline.

## Solution

### Code Change

**File**: `hdl/sram_controller_unified.v`
**Line**: 172

**Before:**
```verilog
if (valid) begin
```

**After:**
```verilog
// Only accept new valid when ready is low (prevents double-trigger)
if (valid && !ready) begin
```

### How It Works

The additional `!ready` condition prevents the controller from accepting a new transaction during the one cycle when:
- `ready` is still high (from completing the previous transaction)
- `valid` hasn't been deasserted yet (adapter waiting for ready)

This ensures proper handshake timing where:
1. Controller asserts `ready` when done
2. Adapter sees `ready`, deasserts `valid`, captures result
3. Controller returns to IDLE with `ready` cleared
4. Only then can a new transaction with `valid && !ready` be accepted

### Debug Output Fix

Also updated simulation debug output at line 461 to match the actual acceptance logic:

```verilog
if (valid && !ready && state == IDLE) begin
    $display("[SRAM_UNIFIED] START: addr=0x%08x wdata=0x%08x wstrb=0x%x",
             addr, wdata, wstrb);
end
```

## Verification

**After Fix:**
```
[SRAM_UNIFIED] START: addr=0x00042000 wdata=0xxxxxxxxx wstrb=0x0
[SRAM_MODEL] READ  addr=0x21000 data=0x0000 @ 414805
[SRAM_MODEL] READ  addr=0x21001 data=0x0000 @ 414845
[SRAM_UNIFIED] COMPLETE READ: addr=0x00042000 rdata=0x00000000
[SRAM_UNIFIED] START: addr=0x00042004 wdata=0xxxxxxxxx wstrb=0x0  ← NEW ADDRESS
[SRAM_MODEL] READ  addr=0x21002 data=0x0000 @ 415045
[SRAM_MODEL] READ  addr=0x21003 data=0x0000 @ 415085
[SRAM_UNIFIED] COMPLETE READ: addr=0x00042004 rdata=0x00000000
```

Each transaction now executes exactly once:
- START → 2 SRAM reads (for 32-bit assembly) → COMPLETE
- No duplicate accesses
- Correct behavior for CPU operation

## Testing

Full-system ModelSim simulation with PicoRV32 CPU confirms:
- ✅ No duplicate transactions
- ✅ Proper valid/ready handshake timing
- ✅ Correct memory read sequencing
- ✅ CPU can successfully read from SRAM

## Impact

This fix is **critical** for correct operation. Without it:
- CPU receives duplicate/incorrect data
- Memory access timing is doubled
- Pipeline synchronization fails
- System cannot boot or execute code

With fix:
- Normal memory operation restored
- Ready for FPGA deployment
- CPU should execute correctly

## Next Steps

1. Build and deploy to FPGA
2. Verify CPU executes firmware correctly
3. Test with LED blink program
4. Compare performance against baseline controller

## Technical Notes

This is a classic example of why valid/ready handshake protocols must be carefully implemented:
- Both signals are registered (updated on clock edges)
- Timing relationships between state transitions matter
- One-cycle hazards can cause subtle but catastrophic bugs
- Simulation is essential for catching these issues

The bug would be nearly impossible to debug on hardware without simulation, as it manifests as complete system failure rather than predictable incorrect behavior.
