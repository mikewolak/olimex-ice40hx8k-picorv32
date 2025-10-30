# SPI DMA Burst Mode Design

## Overview

Upgrade the SPI master to perform true DMA (Direct Memory Access) burst transfers, eliminating CPU overhead for 512-byte SD card block operations.

## Current Performance Issue

**Current "burst mode"** (after fixing double-wait bug):
- CPU writes 512 times to SPI_DATA register
- CPU polls BUSY 512 times
- CPU reads 512 times from SPI_DATA register
- Result: **Same performance as baseline** (just tracks count)

**Target DMA mode**:
- CPU writes start address + count once
- Hardware autonomously transfers 512 bytes from/to SRAM
- CPU polls once at end or gets IRQ
- Result: **Expected 10-20x faster** (limited only by SPI clock)

## Architecture

### New Registers (add to existing 0x80000050-0x8000008F range)

```
0x80000050  SPI_CTRL        Control register (existing)
0x80000054  SPI_DATA        Data register (existing)
0x80000058  SPI_STATUS      Status register (existing)
0x8000005C  SPI_CS          Chip select (existing)
0x80000060  SPI_BURST       Burst count (existing, repurpose)
0x80000064  SPI_DMA_ADDR    DMA source/dest address (NEW)
0x80000068  SPI_DMA_CTRL    DMA control/trigger (NEW)
```

### SPI_DMA_ADDR (0x80000064)
```
[31:0] addr    SRAM address for DMA transfer (must be word-aligned for efficiency)
```

### SPI_DMA_CTRL (0x80000068)
```
[0]    start     Write 1 to start DMA transfer (self-clearing)
[1]    direction 0=read from SRAM (TX), 1=write to SRAM (RX)
[2]    busy      Read-only: 1=DMA in progress, 0=idle
[3]    irq_en    Enable IRQ on completion
```

### SPI_STATUS (update existing 0x80000058)
```
[0]    busy       SPI transfer in progress
[1]    irq        Transfer complete (existing)
[2]    burst_mode Burst mode active (existing, keep for compatibility)
[3]    dma_active DMA engine active (NEW)
```

## Operation Flow

### Firmware TX (SD card write):
```c
void spi_dma_write(const uint8_t *buffer, uint32_t count) {
    SPI_BURST = count;              // Set transfer count
    SPI_DMA_ADDR = (uint32_t)buffer;  // Set source address
    SPI_DMA_CTRL = 0x09;            // start=1, direction=TX, irq_en=1

    // Wait for completion (or use IRQ handler)
    while (SPI_DMA_CTRL & 0x04);    // Poll busy bit

    // Or just return and let IRQ handler notify completion
}
```

### Firmware RX (SD card read):
```c
void spi_dma_read(uint8_t *buffer, uint32_t count) {
    SPI_BURST = count;              // Set transfer count
    SPI_DMA_ADDR = (uint32_t)buffer;  // Set dest address
    SPI_DMA_CTRL = 0x0B;            // start=1, direction=RX, irq_en=1

    while (SPI_DMA_CTRL & 0x04);    // Poll busy bit
}
```

## Hardware Implementation

### SPI Master Module Changes

**New ports** (add to spi_master.v module interface):
```verilog
// DMA Memory Bus (master interface)
output reg        dma_mem_valid,
output reg        dma_mem_write,
output reg [31:0] dma_mem_addr,
output reg [31:0] dma_mem_wdata,
output reg [ 3:0] dma_mem_wstrb,
input wire [31:0] dma_mem_rdata,
input wire        dma_mem_ready
```

**New internal registers**:
```verilog
reg [31:0] dma_addr;        // Current SRAM address
reg [31:0] dma_base_addr;   // Starting address
reg        dma_direction;   // 0=TX (read from SRAM), 1=RX (write to SRAM)
reg        dma_active;      // DMA engine running
reg        dma_irq_en;      // Generate IRQ on completion
reg [7:0]  dma_buffer;      // Single-byte buffer for SRAM<->SPI
```

**New state machine** (DMA controller):
```
States:
- DMA_IDLE:     Waiting for start trigger
- DMA_SETUP:    Initialize transfer
- DMA_READ_MEM: Read byte from SRAM (TX mode)
- DMA_WAIT_MEM: Wait for memory ready
- DMA_WRITE_SPI: Write byte to SPI core
- DMA_WAIT_SPI: Wait for SPI transfer complete
- DMA_READ_SPI: Read byte from SPI core
- DMA_WRITE_MEM: Write byte to SRAM (RX mode)
- DMA_NEXT:     Increment address, decrement count
- DMA_DONE:     Assert IRQ if enabled, return to IDLE
```

### Top-Level Integration (ice40_picorv32_top.v)

**Memory bus arbiter** - Multiplex CPU and SPI DMA onto memory controller:

```verilog
// Memory bus arbiter: CPU has priority, SPI DMA uses idle cycles
wire spi_dma_grant = !cpu_mem_valid && spi_dma_mem_valid;

wire        arb_mem_valid = cpu_mem_valid | (spi_dma_grant ? spi_dma_mem_valid : 1'b0);
wire        arb_mem_write = cpu_mem_valid ? cpu_mem_write : spi_dma_mem_write;
wire [31:0] arb_mem_addr  = cpu_mem_valid ? cpu_mem_addr  : spi_dma_mem_addr;
wire [31:0] arb_mem_wdata = cpu_mem_valid ? cpu_mem_wdata : spi_dma_mem_wdata;
wire [ 3:0] arb_mem_wstrb = cpu_mem_valid ? cpu_mem_wstrb : spi_dma_mem_wstrb;

assign cpu_mem_ready     = arb_mem_ready && cpu_mem_valid;
assign spi_dma_mem_ready = arb_mem_ready && spi_dma_grant;
assign cpu_mem_rdata     = arb_mem_rdata;  // Both see same read data
assign spi_dma_mem_rdata = arb_mem_rdata;
```

**Note**: This arbiter gives CPU absolute priority, which is safe but may cause occasional DMA stalls. For 512-byte transfers this is negligible.

## Performance Analysis

### Current Optimized (non-DMA) Transfer

512-byte transfer overhead (per byte):
- 1x MMIO write to SPI_DATA: ~3 cycles (bus transaction)
- 1x SPI transfer: ~8-16 cycles (depends on clock divider)
- 1x MMIO read from SPI_STATUS: ~3 cycles (poll loop)
- 1x MMIO read from SPI_DATA: ~3 cycles
- Total per byte: ~20-30 cycles minimum

**512 bytes = 10,240-15,360 cycles = 204-307 µs @ 50 MHz**

### DMA Transfer

512-byte transfer overhead (per byte):
- 1x SRAM read/write: ~2-4 cycles (direct memory access)
- 1x SPI transfer: ~8-16 cycles (same)
- Total per byte: ~10-20 cycles

**512 bytes = 5,120-10,240 cycles = 102-204 µs @ 50 MHz**

**Expected speedup: 1.5x-2x** (conservative estimate)

Additional benefit: **CPU is free during transfer** - can do other work!

## Resource Usage Estimate

**LUT increase**: ~100-150 LUTs
- DMA state machine: ~50 LUTs
- Address counter: ~20 LUTs
- Memory bus arbiter: ~30 LUTs
- Control registers: ~30 LUTs

**BRAM**: 0 (no FIFO, single-byte buffering only)

**Timing**: Should easily meet 50 MHz (all register-to-register paths)

Total system LUTs: ~2800 → ~2950 (still well under 7680 limit)

## Implementation Plan

### Phase 1: Add registers and basic DMA controller
1. Add new registers to spi_master.v
2. Implement DMA state machine (without memory bus)
3. Test in simulation with fake memory responses

### Phase 2: Memory bus integration
1. Add DMA memory bus master ports
2. Implement memory bus arbiter in top-level
3. Connect SPI DMA to memory controller

### Phase 3: Simulation verification
1. Create comprehensive testbench
2. Test TX transfer (read from SRAM, send via SPI)
3. Test RX transfer (receive via SPI, write to SRAM)
4. Test CPU priority (CPU access shouldn't stall)
5. Verify IRQ generation

### Phase 4: Firmware update
1. Update io.c with new DMA functions
2. Update sd_spi_optimized.c to use DMA
3. Test on hardware

## Backwards Compatibility

**Keep existing burst mode** for compatibility:
- SPI_BURST register still works as before
- Firmware can still do manual 512-byte loop
- DMA is opt-in via new SPI_DMA_CTRL register

**Register overlap**: None - new registers at 0x64, 0x68

## Safety Considerations

1. **Address validation**: Should firmware validate address ranges? Or trust it?
   - Option A: Hardware checks addr >= 0x00000000 && addr < 0x00080000
   - Option B: Firmware responsible (simpler, fewer LUTs)
   - **Recommendation**: Option B for now (educational platform)

2. **CPU starvation**: DMA gets bus only when CPU idle
   - CPU always has priority
   - Worst case: DMA slightly slower if CPU busy
   - Not a safety issue, just performance

3. **DMA abort**: Should we support mid-transfer abort?
   - Not critical for SD card (512 bytes is fast)
   - Can add later if needed

## Testing Strategy

### Unit Test: DMA State Machine
- Verify state transitions
- Check burst counter decrements correctly
- Verify IRQ pulse on completion

### Integration Test: Memory Bus Arbiter
- Simultaneous CPU + DMA requests
- Verify CPU priority
- Check no deadlocks

### System Test: Full SD Card Transfer
- Write 512 bytes from SRAM to SD card
- Read 512 bytes from SD card to SRAM
- Verify data integrity
- Measure performance vs baseline

## Success Criteria

- ✓ DMA transfers complete without CPU intervention
- ✓ Data integrity: all 512 bytes transferred correctly
- ✓ Performance: ≥1.5x faster than current optimized code
- ✓ No resource issues: fits in 7680 LUTs
- ✓ No timing violations: meets 50 MHz
- ✓ CPU can access memory during DMA without hang

## Risks and Mitigation

**Risk 1**: Memory controller doesn't support multi-master
- Mitigation: Analyzed design, controller is stateless arbiter-friendly
- Backup plan: Time-multiplex access (slower but works)

**Risk 2**: Timing closure issues with arbiter
- Mitigation: Keep arbiter combinational, simple priority logic
- Backup plan: Add pipeline stage (costs 1 cycle latency)

**Risk 3**: SRAM access conflicts cause corruption
- Mitigation: Strict priority arbiter, CPU always wins
- Testing: Stress test with simultaneous access patterns

## Future Enhancements (NOT in scope now)

1. **Scatter-gather DMA**: Transfer from multiple buffers
2. **Double buffering**: Start next transfer while CPU processes previous
3. **Linked-list DMA**: Chain multiple transfers
4. **Burst mode to SRAM**: Use SRAM burst capability for even faster transfers

## Files to Modify

1. `hdl/spi_master.v` - Add DMA engine
2. `hdl/ice40_picorv32_top.v` - Add memory bus arbiter
3. `firmware/sd_fatfs/io.c` - Add DMA transfer functions
4. `firmware/sd_fatfs/io.h` - Add DMA register definitions
5. `firmware/sd_fatfs/sd_spi_optimized.c` - Use DMA functions
6. `sim/spi_dma_tb.v` - New testbench for DMA verification

## Timeline Estimate

- Phase 1 (HDL registers): 2-3 hours
- Phase 2 (Memory bus): 2-3 hours
- Phase 3 (Simulation): 3-4 hours
- Phase 4 (Firmware + HW test): 2-3 hours
- **Total: 9-13 hours** (1-2 days)

---

**Status**: Design complete, ready for implementation
**Author**: Michael Wolak (with Claude Code assistance)
**Date**: October 29, 2025
