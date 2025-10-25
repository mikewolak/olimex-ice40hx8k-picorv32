# SPI Master BSRAM FIFO Enhancement

**Date**: October 2025
**Author**: Michael Wolak
**Status**: Work In Progress - DO NOT COMMIT YET

---

## Problem Statement

Current SPI implementation transfers **1 byte per transaction** with significant CPU overhead:
- **Current throughput**: ~60 KB/s (limited by software dispatch overhead)
- **Theoretical max**: 3.125 MB/s at 50 MHz SPI clock
- **Bottleneck**: ~833 CPU cycles spent per byte on MMIO polling and dispatch

Each byte requires:
1. Poll BUSY flag (wait for ready)
2. Write to SPI_DATA register
3. Poll BUSY flag again (wait for completion)
4. Software loop overhead

---

## Solution: BSRAM-Based Burst Transfer FIFO

### Resource Usage
- **Current BRAM usage**: 14 / 32 blocks (synthesis output shows 14 SB_RAM40_4K instances)
- **Proposed addition**: 2 BRAM blocks (TX FIFO + RX FIFO)
- **Final usage**: 16 / 32 blocks (50% utilization)
- **LUT overhead**: <100 LUTs (FIFO pointers, control FSM)

### Architecture

**Dual BSRAM FIFOs**:
1. **TX FIFO**: 512 bytes (1 x SB_RAM40_4K configured as 512x8)
2. **RX FIFO**: 512 bytes (1 x SB_RAM40_4K configured as 512x8)

**Dual-port operation**:
- **Write port**: CPU writes burst data to FIFO
- **Read port**: SPI FSM reads/writes during burst transfer
- **Simultaneous access**: CPU can fill next buffer while SPI sends current buffer

---

## Memory Map Changes

### Existing Registers (Unchanged)
```
0x80000050: SPI_CTRL   - Control register (CPOL, CPHA, CLK_DIV)
0x8000005C: SPI_CS     - Chip select control
```

### Modified Registers
```
0x80000054: SPI_DATA
  WRITE: Write byte to TX FIFO (if FIFO not full)
         In legacy mode (XFER_COUNT=0): Immediate single-byte transfer
  READ:  Read byte from RX FIFO

0x80000058: SPI_STATUS
  Bit 0:     BUSY (1 = transfer in progress)
  Bit 1:     DONE (1 = transfer complete, !BUSY)
  Bit 2:     TX_FIFO_FULL (1 = TX FIFO full, cannot write)
  Bit 3:     TX_FIFO_EMPTY (1 = TX FIFO empty)
  Bit 4:     RX_FIFO_FULL (1 = RX FIFO full)
  Bit 5:     RX_FIFO_EMPTY (1 = RX FIFO empty)
  Bits 15-6: (reserved)
  Bits 24-16: TX_FIFO_LEVEL (0-512 bytes in TX FIFO)
```

### New Registers
```
0x80000060: SPI_XFER_COUNT
  WRITE: Start burst transfer of N bytes from TX FIFO (1-512)
         Writing 0 enters legacy mode (single-byte direct transfers)
  READ:  Bytes remaining in current burst transfer (countdown)

0x80000064: SPI_FIFO_STATUS
  Bits 8-0:   TX_FIFO_LEVEL (0-512 bytes in TX FIFO)
  Bits 24-16: RX_FIFO_LEVEL (0-512 bytes in RX FIFO)
```

---

## Operation Modes

### Mode 1: Legacy Single-Byte (Backward Compatible)
**No code changes required** - existing firmware continues to work.

```c
// Works exactly as before
SPI_DATA = 0xAB;  // Direct transfer (XFER_COUNT=0)
while (SPI_STATUS & BUSY);
uint8_t rx = SPI_DATA;
```

**Behavior**: Write to SPI_DATA immediately starts transfer if XFER_COUNT=0 (legacy mode).

### Mode 2: Burst Transfer (NEW)
```c
// Fill TX FIFO (fast MMIO writes, no SPI activity yet)
for (int i = 0; i < 512; i++) {
    while (SPI_STATUS & (1 << 2));  // Wait if FIFO full
    SPI_DATA = tx_buffer[i];
}

// Start burst transfer (hardware sends all 512 bytes automatically)
SPI_XFER_COUNT = 512;

// Wait for completion (single IRQ when entire burst done)
while (SPI_STATUS & BUSY);

// Read RX FIFO
for (int i = 0; i < 512; i++) {
    while (SPI_STATUS & (1 << 5));  // Wait if RX FIFO empty
    rx_buffer[i] = SPI_DATA;
}
```

**Behavior**:
1. CPU fills TX FIFO at full memory speed (~50 MHz)
2. Single write to XFER_COUNT starts hardware burst
3. SPI FSM autonomously transfers all bytes
4. Single interrupt when complete
5. CPU reads RX FIFO at full memory speed

### Mode 3: Pipelined Double-Buffering (MAXIMUM PERFORMANCE)
```c
// Fill first TX FIFO burst
for (int i = 0; i < 256; i++) SPI_DATA = block1[i];
SPI_XFER_COUNT = 256;

// While SPI sends first burst, prepare second burst
for (int i = 0; i < 256; i++) {
    while (SPI_STATUS & (1 << 2));  // Wait if FIFO full
    SPI_DATA = block2[i];
}

// Wait for first burst completion
while (SPI_STATUS & BUSY);

// Read first RX burst
for (int i = 0; i < 256; i++) rx_block1[i] = SPI_DATA;

// Start second burst immediately (no idle time)
SPI_XFER_COUNT = 256;
```

**Behavior**: Zero idle time between bursts - maximum sustained throughput.

---

## HDL Implementation Details

### BSRAM Primitive Configuration

iCE40 `SB_RAM40_4K` block has multiple configurations:
- **256x16** (READ_MODE=0, WRITE_MODE=0)
- **512x8**  (READ_MODE=1, WRITE_MODE=1) ← **We use this**
- **1024x4** (READ_MODE=2, WRITE_MODE=2)
- **2048x2** (READ_MODE=3, WRITE_MODE=3)

### TX FIFO Instantiation
```verilog
SB_RAM40_4K #(
    .READ_MODE(1),    // 512x8 configuration
    .WRITE_MODE(1)    // 512x8 configuration
) tx_fifo (
    // Write port (CPU fills FIFO)
    .WDATA(mmio_wdata[15:0]),  // 16-bit interface, use [7:0]
    .WADDR(tx_wr_ptr[8:0]),    // 9-bit address (0-511)
    .MASK(16'h0000),           // No masking
    .WE(tx_fifo_wr_en),
    .WCLK(clk),
    .WCLKE(1'b1),

    // Read port (SPI FSM consumes FIFO)
    .RDATA(tx_fifo_rd_data),   // 16-bit, use [7:0]
    .RADDR(tx_rd_ptr[8:0]),
    .RE(tx_fifo_rd_en),
    .RCLK(clk),
    .RCLKE(1'b1)
);
```

### RX FIFO Instantiation
```verilog
SB_RAM40_4K #(
    .READ_MODE(1),    // 512x8 configuration
    .WRITE_MODE(1)
) rx_fifo (
    // Write port (SPI FSM fills FIFO)
    .WDATA({8'h00, rx_data_capture}),
    .WADDR(rx_wr_ptr[8:0]),
    .MASK(16'h0000),
    .WE(rx_fifo_wr_en),
    .WCLK(clk),
    .WCLKE(1'b1),

    // Read port (CPU reads FIFO)
    .RDATA(rx_fifo_rd_data),
    .RADDR(rx_rd_ptr[8:0]),
    .RE(rx_fifo_rd_en),
    .RCLK(clk),
    .RCLKE(1'b1)
);
```

### FIFO Control Logic
```verilog
// TX FIFO pointers
reg [8:0] tx_wr_ptr;      // Write pointer (CPU)
reg [8:0] tx_rd_ptr;      // Read pointer (SPI FSM)
reg [9:0] tx_fifo_level;  // Occupancy (0-512)

wire tx_fifo_full  = (tx_fifo_level == 10'd512);
wire tx_fifo_empty = (tx_fifo_level == 10'd0);

// RX FIFO pointers
reg [8:0] rx_wr_ptr;      // Write pointer (SPI FSM)
reg [8:0] rx_rd_ptr;      // Read pointer (CPU)
reg [9:0] rx_fifo_level;  // Occupancy (0-512)

wire rx_fifo_full  = (rx_fifo_level == 10'd512);
wire rx_fifo_empty = (rx_fifo_level == 10'd0);

// Transfer control
reg [9:0] xfer_count;     // Burst size (1-512)
reg [9:0] xfer_remaining; // Countdown during burst
wire burst_mode = (xfer_count > 0);
```

### Modified SPI State Machine

**STATE_IDLE**:
```verilog
if (burst_mode && !tx_fifo_empty) begin
    // Burst mode: fetch from TX FIFO
    tx_fifo_rd_en <= 1'b1;
    shift_reg <= tx_fifo_rd_data[7:0];
    xfer_remaining <= xfer_count - 1'b1;
    state <= STATE_TRANSMIT;
end
else if (!burst_mode && legacy_tx_valid) begin
    // Legacy mode: direct single-byte transfer
    shift_reg <= tx_data;
    state <= STATE_TRANSMIT;
end
```

**STATE_FINISH**:
```verilog
// Store RX byte to RX FIFO
if (burst_mode) begin
    rx_fifo_wr_en <= 1'b1;
    rx_wr_ptr <= rx_wr_ptr + 1'b1;
    rx_fifo_level <= rx_fifo_level + 1'b1;
end else begin
    rx_data <= shift_reg;  // Legacy direct register
end

// Continue burst or finish
if (xfer_remaining > 0 && !tx_fifo_empty) begin
    tx_fifo_rd_en <= 1'b1;
    shift_reg <= tx_fifo_rd_data[7:0];
    xfer_remaining <= xfer_remaining - 1'b1;
    state <= STATE_TRANSMIT;
end
else begin
    irq_pulse <= 1'b1;  // Burst complete
    state <= STATE_IDLE;
end
```

---

## Performance Analysis

### Current Performance (1-byte dispatch)
- **Overhead per byte**: ~833 cycles (poll + write + poll + loop)
- **SPI transfer time** at 50 MHz: 16 cycles/byte
- **Total**: ~849 cycles/byte
- **Throughput**: 50 MHz ÷ 849 = **58.9 KB/s** ✓ (matches measured 60 KB/s)

### Projected Performance (512-byte burst)

**CPU overhead (one-time per burst)**:
- Fill TX FIFO: 512 writes × 2 cycles = 1,024 cycles
- Start burst: 1 write = 2 cycles
- Wait for IRQ: 0 cycles (can do other work)
- Read RX FIFO: 512 reads × 2 cycles = 1,024 cycles
- **Total CPU**: ~2,050 cycles

**SPI transfer time (overlapped with CPU)**:
- 512 bytes × 16 clocks/byte = 8,192 cycles at 50 MHz SPI

**Total time**: max(2,050, 8,192) = 8,192 cycles (SPI bound)

**Throughput**: 50 MHz ÷ 8,192 × 512 bytes = **3.125 MB/s**

**Improvement**: 3.125 MB/s ÷ 60 KB/s = **52x faster** 🚀

### Realistic Performance at Lower SPI Clocks

| SPI Clock | Cycles/Burst | Throughput | Improvement |
|-----------|--------------|------------|-------------|
| 50 MHz    | 8,192        | 3.125 MB/s | 52x         |
| 25 MHz    | 16,384       | 1.562 MB/s | 26x         |
| 12.5 MHz  | 32,768       | 781 KB/s   | 13x         |
| 6.25 MHz  | 65,536       | 390 KB/s   | 6.5x        |

Even at SD card initialization speed (390 kHz), burst mode helps:
- Old: 60 KB/s
- New: 390 kHz ÷ 8 bits × efficiency ~= **40-45 KB/s** (no improvement)

**Conclusion**: Burst mode shines at medium-to-high SPI clocks where CPU overhead dominates.

---

## Backward Compatibility

**Guaranteed**: Existing firmware works without modification.

**Mechanism**:
- On reset, `xfer_count = 0` (burst mode disabled)
- Write to `SPI_DATA` triggers immediate transfer when `xfer_count == 0`
- Legacy single-byte behavior preserved exactly

**Testing**:
- All existing test cases (loopback, speed test, SD init) must pass unchanged
- Then add new burst transfer tests

---

## Testing Plan

### Phase 1: Synthesis Verification
- [ ] Synthesize with new FIFO logic
- [ ] Verify BRAM usage: 16 blocks (14 existing + 2 new)
- [ ] Verify LUT increase: <100 LUTs
- [ ] Verify timing closure: No new critical paths

### Phase 2: Simulation (if time permits)
- [ ] Verify FIFO write/read pointers
- [ ] Verify burst mode state machine
- [ ] Verify legacy mode still works

### Phase 3: Hardware Testing
- [ ] Test legacy single-byte loopback (must pass)
- [ ] Test burst loopback (16, 64, 256, 512 bytes)
- [ ] Measure throughput improvement
- [ ] Test with real SD card block transfers

### Phase 4: Software Integration
- [ ] Add burst transfer API to spi_test.c
- [ ] Update performance test to use burst mode
- [ ] Add configurable burst sizes (16/32/64/128/256/512)

---

## Rollback Plan

If synthesis fails or doesn't fit:

1. **Git revert**: Branch `spi-fifo` can be abandoned
2. **Fallback option**: Reduce FIFO size (256 bytes = 1 BRAM instead of 2)
3. **Minimal option**: 64-byte FIFO using registers (no BRAM)

---

## Implementation Checklist

- [ ] Create design document (this file)
- [ ] Create git branch `spi-fifo`
- [ ] Back up original `spi_master.v`
- [ ] Implement BSRAM FIFO in `spi_master.v`
- [ ] Test synthesis (verify BRAM and LUT usage)
- [ ] Test place-and-route (verify timing)
- [ ] Build bitstream
- [ ] Test on hardware (legacy mode)
- [ ] Update software for burst mode
- [ ] Test on hardware (burst mode)
- [ ] Document final performance results
- [ ] Commit if successful

---

## Git Branch Strategy

**Branch name**: `spi-fifo`
**Base**: `master` (current HEAD: f691c34)
**Merge strategy**: Only merge after hardware testing confirms improvement

**Commit message template**:
```
Add BSRAM FIFO burst transfer support to SPI master

Implements 512-byte TX/RX FIFOs using iCE40 block RAM for burst
SPI transfers, addressing CPU overhead bottleneck in single-byte
dispatch mode.

Hardware changes:
- Add 2x SB_RAM40_4K blocks (512x8 TX FIFO, 512x8 RX FIFO)
- New registers: SPI_XFER_COUNT, SPI_FIFO_STATUS
- Modified SPI_STATUS with FIFO flags
- Backward compatible legacy single-byte mode

Performance:
- Old: 60 KB/s (1-byte dispatch overhead limited)
- New: 3.125 MB/s (512-byte burst at 50 MHz SPI clock)
- Improvement: 52x faster

Resource usage:
- BRAM: 14 → 16 blocks (out of 32 available)
- LUTs: +~80 (FIFO control logic)

Tested: Loopback, SD card block transfers, sustained throughput.
```

---

**End of Design Document**
