# SPI DMA Implementation Checklist

## Status: READY TO IMPLEMENT
**Design complete, test firmware ready, backed up current code**

## Implementation Order

### Phase 1: HDL - SPI Master DMA Engine (hdl/spi_master.v)

#### Task 1.1: Add DMA memory bus master ports
Add to module interface after line 39:
```verilog
    // DMA Memory Bus (master interface) - NEW
    output reg        dma_mem_valid,
    output reg        dma_mem_write,
    output reg [31:0] dma_mem_addr,
    output reg [31:0] dma_mem_wdata,
    output reg [ 3:0] dma_mem_wstrb,
    input wire [31:0] dma_mem_rdata,
    input wire        dma_mem_ready
```

#### Task 1.2: Add DMA register addresses
Add after line 48:
```verilog
    localparam ADDR_SPI_DMA_ADDR = 32'h80000064;  // DMA address register
    localparam ADDR_SPI_DMA_CTRL = 32'h80000068;  // DMA control register
```

####  Task 1.3: Add DMA internal registers
Add after line 71:
```verilog
    //==========================================================================
    // DMA Registers (NEW)
    //==========================================================================
    reg [31:0] dma_addr;         // Current SRAM address
    reg [31:0] dma_base_addr;    // Starting SRAM address
    reg        dma_direction;    // 0=TX (SRAM->SPI), 1=RX (SPI->SRAM)
    reg        dma_active;       // DMA engine active
    reg        dma_irq_en;       // Generate IRQ on DMA complete
    reg [7:0]  dma_buffer;       // Single-byte buffer for transfers

    // DMA state machine
    localparam DMA_IDLE      = 4'd0;
    localparam DMA_SETUP     = 4'd1;
    localparam DMA_READ_MEM  = 4'd2;
    localparam DMA_WAIT_MEM  = 4'd3;
    localparam DMA_WRITE_SPI = 4'd4;
    localparam DMA_WAIT_SPI  = 4'd5;
    localparam DMA_READ_SPI  = 4'd6;
    localparam DMA_WRITE_MEM = 4'd7;
    localparam DMA_NEXT      = 4'd8;
    localparam DMA_DONE      = 4'd9;

    reg [3:0] dma_state;
```

#### Task 1.4: Update STATUS register
Find STATUS register read (around line 346), change from:
```verilog
mmio_rdata <= {29'h0, burst_mode, ~busy, busy};
```
To:
```verilog
mmio_rdata <= {28'h0, dma_active, burst_mode, ~busy, busy};
```

#### Task 1.5: Add DMA register MMIO handlers
Add new cases in MMIO write handler (after ADDR_SPI_BURST case):
```verilog
                ADDR_SPI_DMA_ADDR: begin
                    if (mmio_wstrb[0]) dma_base_addr[7:0]   <= mmio_wdata[7:0];
                    if (mmio_wstrb[1]) dma_base_addr[15:8]  <= mmio_wdata[15:8];
                    if (mmio_wstrb[2]) dma_base_addr[23:16] <= mmio_wdata[23:16];
                    if (mmio_wstrb[3]) dma_base_addr[31:24] <= mmio_wdata[31:24];
                    mmio_ready <= 1'b1;
                end

                ADDR_SPI_DMA_CTRL: begin
                    if (mmio_wstrb[0]) begin
                        dma_direction <= mmio_wdata[1];
                        dma_irq_en    <= mmio_wdata[3];
                        // Start bit (bit 0): triggers DMA if not already active
                        if (mmio_wdata[0] && !dma_active && burst_count > 0) begin
                            dma_state  <= DMA_SETUP;
                            dma_active <= 1'b1;
                        end
                    end
                    mmio_ready <= 1'b1;
                end
```

Add new cases in MMIO read handler:
```verilog
                ADDR_SPI_DMA_ADDR: begin
                    mmio_rdata <= dma_base_addr;
                    mmio_ready <= 1'b1;
                end

                ADDR_SPI_DMA_CTRL: begin
                    mmio_rdata <= {29'h0, dma_irq_en, dma_active, dma_direction, 1'b0};
                    mmio_ready <= 1'b1;
                end
```

#### Task 1.6: Implement DMA state machine
Add new always block after the SPI core state machine:
```verilog
    //==========================================================================
    // DMA Engine State Machine
    //==========================================================================
    always @(posedge clk) begin
        if (!resetn) begin
            dma_state     <= DMA_IDLE;
            dma_active    <= 1'b0;
            dma_addr      <= 32'h0;
            dma_buffer    <= 8'h0;
            dma_mem_valid <= 1'b0;
            dma_mem_write <= 1'b0;
            dma_mem_addr  <= 32'h0;
            dma_mem_wdata <= 32'h0;
            dma_mem_wstrb <= 4'h0;
        end else begin
            // Default: clear memory bus valid
            if (dma_mem_ready) begin
                dma_mem_valid <= 1'b0;
            end

            case (dma_state)
                DMA_IDLE: begin
                    // Waiting for start trigger (handled in MMIO write)
                    dma_active <= 1'b0;
                end

                DMA_SETUP: begin
                    // Initialize DMA transfer
                    dma_addr  <= dma_base_addr;
                    if (dma_direction == 1'b0) begin
                        // TX mode: read from SRAM first
                        dma_state <= DMA_READ_MEM;
                    end else begin
                        // RX mode: read from SPI first
                        dma_state <= DMA_READ_SPI;
                    end
                end

                DMA_READ_MEM: begin
                    // Request byte from SRAM
                    dma_mem_valid <= 1'b1;
                    dma_mem_write <= 1'b0;
                    dma_mem_addr  <= dma_addr;
                    dma_mem_wstrb <= 4'b0000;
                    dma_state     <= DMA_WAIT_MEM;
                end

                DMA_WAIT_MEM: begin
                    if (dma_mem_ready) begin
                        // Store byte from memory
                        dma_buffer    <= dma_mem_rdata[7:0];
                        dma_mem_valid <= 1'b0;

                        if (dma_direction == 1'b0) begin
                            // TX: write to SPI next
                            dma_state <= DMA_WRITE_SPI;
                        end else begin
                            // RX: done with this byte
                            dma_state <= DMA_NEXT;
                        end
                    end
                end

                DMA_WRITE_SPI: begin
                    // Write byte to SPI core (use existing tx_data/tx_valid mechanism)
                    if (!busy) begin
                        tx_data   <= dma_buffer;
                        tx_valid  <= 1'b1;
                        dma_state <= DMA_WAIT_SPI;
                    end
                end

                DMA_WAIT_SPI: begin
                    tx_valid <= 1'b0;
                    if (done) begin
                        if (dma_direction == 1'b0) begin
                            // TX: done with this byte
                            dma_state <= DMA_NEXT;
                        end else begin
                            // RX: read received byte
                            dma_state <= DMA_READ_SPI;
                        end
                    end
                end

                DMA_READ_SPI: begin
                    // Read byte from SPI core
                    dma_buffer <= rx_data;
                    dma_state  <= DMA_WRITE_MEM;
                end

                DMA_WRITE_MEM: begin
                    // Write byte to SRAM
                    dma_mem_valid <= 1'b1;
                    dma_mem_write <= 1'b1;
                    dma_mem_addr  <= dma_addr;
                    dma_mem_wdata <= {24'h0, dma_buffer};
                    dma_mem_wstrb <= 4'b0001;  // Byte write
                    dma_state     <= DMA_WAIT_MEM;
                end

                DMA_NEXT: begin
                    // Increment address, decrement count
                    dma_addr <= dma_addr + 1'b1;

                    if (burst_count > 1) begin
                        // More bytes to transfer
                        burst_count <= burst_count - 1'b1;

                        if (dma_direction == 1'b0) begin
                            dma_state <= DMA_READ_MEM;  // TX: read next byte
                        end else begin
                            // RX: Start SPI transfer first
                            if (!busy) begin
                                tx_data  <= 8'hFF;  // Dummy byte for RX
                                tx_valid <= 1'b1;
                                dma_state <= DMA_WAIT_SPI;
                            end
                        end
                    end else begin
                        // Transfer complete
                        burst_count <= 13'h0;
                        burst_mode  <= 1'b0;
                        dma_state   <= DMA_DONE;
                    end
                end

                DMA_DONE: begin
                    dma_active <= 1'b0;
                    if (dma_irq_en) begin
                        irq_pulse <= 1'b1;  // Generate IRQ
                    end
                    dma_state <= DMA_IDLE;
                end

                default: dma_state <= DMA_IDLE;
            endcase
        end
    end
```

### Phase 2: HDL - Top-Level Integration (hdl/ice40_picorv32_top.v)

#### Task 2.1: Add SPI DMA memory bus signals
Add after CPU memory bus wires (around line 161):
```verilog
    // SPI DMA memory bus (master interface)
    wire        spi_dma_mem_valid;
    wire        spi_dma_mem_write;
    wire [31:0] spi_dma_mem_addr;
    wire [31:0] spi_dma_mem_wdata;
    wire [ 3:0] spi_dma_mem_wstrb;
    wire [31:0] spi_dma_mem_rdata;
    wire        spi_dma_mem_ready;
```

#### Task 2.2: Add memory bus arbiter
Add before memory controller instantiation (around line 270):
```verilog
    //==========================================================================
    // Memory Bus Arbiter (CPU priority, SPI DMA uses idle cycles)
    //==========================================================================
    wire spi_dma_grant = !cpu_mem_valid && spi_dma_mem_valid;

    wire        arb_mem_valid = cpu_mem_valid | (spi_dma_grant & spi_dma_mem_valid);
    wire        arb_mem_write = cpu_mem_valid ? cpu_mem_write : spi_dma_mem_write;
    wire [31:0] arb_mem_addr  = cpu_mem_valid ? cpu_mem_addr  : spi_dma_mem_addr;
    wire [31:0] arb_mem_wdata = cpu_mem_valid ? cpu_mem_wdata : spi_dma_mem_wdata;
    wire [ 3:0] arb_mem_wstrb = cpu_mem_valid ? cpu_mem_wstrb : spi_dma_mem_wstrb;
    wire [31:0] arb_mem_rdata;
    wire        arb_mem_ready;

    assign cpu_mem_ready     = arb_mem_ready & cpu_mem_valid;
    assign spi_dma_mem_ready = arb_mem_ready & spi_dma_grant;
    assign cpu_mem_rdata     = arb_mem_rdata;
    assign spi_dma_mem_rdata = arb_mem_rdata;
```

#### Task 2.3: Update memory controller connection
Change memory controller inputs from `cpu_mem_*` to `arb_mem_*`:
```verilog
    sram_unified_adapter mem_controller (
        ...
        .cpu_mem_valid(arb_mem_valid),   // Changed from cpu_mem_valid
        .cpu_mem_write(arb_mem_write),   // Changed from cpu_mem_write
        .cpu_mem_addr(arb_mem_addr),     // Changed from cpu_mem_addr
        .cpu_mem_wdata(arb_mem_wdata),   // Changed from cpu_mem_wdata
        .cpu_mem_wstrb(arb_mem_wstrb),   // Changed from cpu_mem_wstrb
        .cpu_mem_rdata(arb_mem_rdata),   // Changed from cpu_mem_rdata
        .cpu_mem_ready(arb_mem_ready),   // Changed from cpu_mem_ready
        ...
    );
```

#### Task 2.4: Connect SPI DMA to SPI master
Add to spi_master instantiation:
```verilog
    spi_master spi (
        ...existing ports...
        // DMA memory bus (NEW)
        .dma_mem_valid(spi_dma_mem_valid),
        .dma_mem_write(spi_dma_mem_write),
        .dma_mem_addr(spi_dma_mem_addr),
        .dma_mem_wdata(spi_dma_mem_wdata),
        .dma_mem_wstrb(spi_dma_mem_wstrb),
        .dma_mem_rdata(spi_dma_mem_rdata),
        .dma_mem_ready(spi_dma_mem_ready)
    );
```

### Phase 3: Firmware Build

#### Task 3.1: Add firmware build target
Already created: `firmware/spi_dma_test.c`

Need to add to Makefile as bare-metal target (no newlib needed).

### Phase 4: Simulation

#### Task 4.1: Create simulation script
File: `sim/run_spi_dma_test.do`
```tcl
# Compile full system
vlib work
vlog -sv ../hdl/*.v
vlog -sv full_system_tb.v

# Run with spi_dma_test firmware
vsim -c work.full_system_tb -gFIRMWARE_FILE="../firmware/spi_dma_test.bin"
run 100ms
quit
```

#### Task 4.2: Monitor simulation output
Watch for test firmware UART output:
- "SPI DMA Comprehensive Test"
- Register access tests
- DMA TX transfer
- DMA RX transfer
- Data integrity verification
- Performance comparison
- "ALL TESTS PASSED!"

## Expected Results

### Success Criteria
1. Firmware loads and runs
2. DMA register reads/writes work
3. DMA TX transfers 512 bytes SRAM -> SPI
4. DMA RX transfers 512 bytes SPI -> SRAM
5. All data matches expected pattern
6. DMA faster than manual transfer
7. No timing violations
8. Fits in FPGA (< 7680 LUTs)

### Performance Target
- DMA transfer: < 10,000 cycles for 512 bytes
- Manual transfer: > 15,000 cycles for 512 bytes
- **Speedup: ≥ 1.5x**

## Debugging Notes

### Common Issues

**Issue**: Verilog compilation errors
**Fix**: Check syntax, port list, case sensitivity

**Issue**: Simulation hangs
**Fix**: Check DMA state machine transitions, ensure all states exit

**Issue**: Memory corruption
**Fix**: Verify arbiter priority, check wstrb for byte writes

**Issue**: Data mismatch
**Fix**: Verify SPI loopback in testbench, check endianness

**Issue**: DMA not starting
**Fix**: Check burst_count > 0, verify start bit write

**Issue**: Performance worse than expected
**Fix**: Check for extra wait states in DMA state machine

## Backup/Rollback

**Current code backed up to**:
- `hdl/spi_master_pre_dma.v`

**To rollback**:
```bash
cp hdl/spi_master_pre_dma.v hdl/spi_master.v
```

## Next Steps After Successful Simulation

1. Synthesize for FPGA
2. Check resource usage (should be ~2950 LUTs)
3. Check timing (should meet 50 MHz)
4. Program FPGA
5. Test with real SD card
6. Measure actual performance improvement

---

**Status**: Implementation plan complete, ready to code
**Date**: October 29, 2025
**Est. Time**: 9-13 hours for full implementation + testing
