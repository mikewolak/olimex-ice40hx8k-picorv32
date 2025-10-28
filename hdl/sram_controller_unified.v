//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// sram_controller_unified.v - Optimized Unified SRAM Controller
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//
//==============================================================================
// PERFORMANCE: 4-7 cycle 32-bit access (vs 11-29 cycles in 3-layer design)
//
// KEY OPTIMIZATIONS:
// - Single unified state machine (no handshaking overhead)
// - Direct 16-bit SRAM control (no intermediate layers)
// - 2-cycle 16-bit access (SETUP + PULSE/CAPTURE)
// - Eliminated COOLDOWN, WAIT, RECOVERY states (unnecessary per SRAM specs)
// - Smart byte-strobe handling (aligned halfword = direct write, no RMW)
//
// TIMING VALIDATION @ 50MHz (20ns/cycle):
// - 32-bit read/write: 4 cycles = 80ns
// - Byte write (RMW): 7 cycles = 140ns
// - Halfword write (aligned): 4 cycles = 80ns
//
// SRAM CHIP: IS61WV51216BLL-10TLI (512KB, 16-bit, 10ns access)
// - tAA (address access): 10ns max → 20ns provided ✓
// - tWP (write pulse): 7ns min → 20ns provided ✓
// - tWR (write recovery): 0ns min → not needed ✓
//==============================================================================

module sram_controller_unified (
    input wire clk,
    input wire resetn,

    // CPU Interface (32-bit)
    input wire valid,
    output reg ready,
    input wire [3:0] wstrb,          // Byte strobes: [3:0] = bytes [3:0]
    input wire [31:0] addr,          // Byte address from CPU
    input wire [31:0] wdata,
    output reg [31:0] rdata,

    // SRAM Physical Interface (16-bit)
    output reg [17:0] sram_addr,     // 18-bit word address (256K x 16)
    inout wire [15:0] sram_data,
    output reg sram_cs_n,
    output reg sram_oe_n,
    output reg sram_we_n
);

    //==========================================================================
    // State Machine
    //==========================================================================
    localparam IDLE              = 4'd0;

    // Full 32-bit read (wstrb == 4'b0000)
    localparam READ_LOW_SETUP    = 4'd1;
    localparam READ_LOW_CAPTURE  = 4'd2;
    localparam READ_HIGH_SETUP   = 4'd3;
    localparam READ_HIGH_CAPTURE = 4'd4;

    // Full 32-bit write (wstrb == 4'b1111)
    localparam WRITE_LOW_SETUP   = 4'd5;
    localparam WRITE_LOW_PULSE   = 4'd6;
    localparam WRITE_HIGH_SETUP  = 4'd7;
    localparam WRITE_HIGH_PULSE  = 4'd8;

    // Partial write (byte/halfword - need read-modify-write)
    localparam RMW_READ_LOW_SETUP   = 4'd9;
    localparam RMW_READ_LOW_CAPTURE = 4'd10;
    localparam RMW_READ_HIGH_SETUP  = 4'd11;
    localparam RMW_READ_HIGH_CAPTURE = 4'd12;
    localparam RMW_WRITE_LOW_SETUP  = 4'd13;
    localparam RMW_WRITE_LOW_PULSE  = 4'd14;

    // Write completion states (deassert WE cleanly)
    localparam WRITE_LOW_COMPLETE     = 5'd15;
    localparam WRITE_HIGH_COMPLETE    = 5'd16;
    localparam RMW_WRITE_LOW_COMPLETE = 5'd17;
    localparam RMW_WRITE_HIGH_SETUP   = 5'd18;
    localparam RMW_WRITE_HIGH_PULSE   = 5'd19;
    localparam RMW_WRITE_HIGH_COMPLETE = 5'd20;

    reg [4:0] state;

    //==========================================================================
    // Internal Registers
    //==========================================================================
    reg [31:0] addr_reg;        // Latched byte address
    reg [31:0] wdata_reg;       // Latched write data
    reg [3:0] wstrb_reg;        // Latched write strobes
    reg [15:0] rdata_low;       // LOW halfword buffer
    reg [15:0] rdata_high;      // HIGH halfword buffer
    reg [15:0] data_out_reg;    // Data to drive on SRAM bus
    reg data_oe;                // Output enable for tri-state buffer

    // Tri-state control for SRAM data bus
    assign sram_data = data_oe ? data_out_reg : 16'hzzzz;

    //==========================================================================
    // Write Strobe Decode
    //==========================================================================
    wire is_full_write = (wstrb_reg == 4'b1111);
    wire is_read = (wstrb_reg == 4'b0000);
    wire is_partial_write = (wstrb_reg != 4'b1111) && (wstrb_reg != 4'b0000);

    // Check if aligned halfword write (can skip RMW)
    wire is_low_halfword_aligned = (wstrb_reg == 4'b0011);   // Bytes [1:0]
    wire is_high_halfword_aligned = (wstrb_reg == 4'b1100);  // Bytes [3:2]
    wire is_aligned_halfword = is_low_halfword_aligned || is_high_halfword_aligned;

    // Determine which halfwords are affected
    wire low_halfword_affected = (wstrb_reg[1:0] != 2'b00);
    wire high_halfword_affected = (wstrb_reg[3:2] != 2'b00);

    //==========================================================================
    // Address Calculation
    //==========================================================================
    // CPU provides byte address, SRAM needs word address (divide by 2)
    // addr_reg[31:1] = word address
    // addr_reg[0] = ignored (aligned to 16-bit boundaries)

    wire [17:0] sram_addr_low  = addr_reg[18:1];      // LOW halfword address
    wire [17:0] sram_addr_high = addr_reg[18:1] + 1;  // HIGH halfword address

    //==========================================================================
    // Read-Modify-Write Data Merging
    //==========================================================================
    wire [15:0] merged_low;
    wire [15:0] merged_high;

    // Merge LOW halfword based on byte strobes
    assign merged_low[7:0]  = wstrb_reg[0] ? wdata_reg[7:0]  : rdata_low[7:0];
    assign merged_low[15:8] = wstrb_reg[1] ? wdata_reg[15:8] : rdata_low[15:8];

    // Merge HIGH halfword based on byte strobes
    assign merged_high[7:0]  = wstrb_reg[2] ? wdata_reg[23:16] : rdata_high[7:0];
    assign merged_high[15:8] = wstrb_reg[3] ? wdata_reg[31:24] : rdata_high[15:8];

    //==========================================================================
    // Main State Machine
    //==========================================================================
    always @(posedge clk) begin
        if (!resetn) begin
            state <= IDLE;
            ready <= 1'b0;
            sram_cs_n <= 1'b1;
            sram_oe_n <= 1'b1;
            sram_we_n <= 1'b1;
            data_oe <= 1'b0;
            addr_reg <= 32'h0;
            wdata_reg <= 32'h0;
            wstrb_reg <= 4'h0;
            rdata <= 32'h0;
            rdata_low <= 16'h0;
            rdata_high <= 16'h0;
            data_out_reg <= 16'h0;
        end else begin
            case (state)
                //==============================================================
                // IDLE: Wait for CPU transaction
                //==============================================================
                IDLE: begin
                    ready <= 1'b0;
                    sram_cs_n <= 1'b1;
                    sram_oe_n <= 1'b1;
                    sram_we_n <= 1'b1;
                    data_oe <= 1'b0;

                    // Only accept new valid when ready is low (prevents double-trigger)
                    if (valid && !ready) begin
                        // Latch inputs
                        addr_reg <= addr;
                        wdata_reg <= wdata;
                        wstrb_reg <= wstrb;

                        // Decode operation type and branch
                        if (wstrb == 4'b0000) begin
                            // Full read
                            state <= READ_LOW_SETUP;
                        end else if (wstrb == 4'b1111) begin
                            // Full 32-bit write
                            state <= WRITE_LOW_SETUP;
                        end else if (is_aligned_halfword) begin
                            // Aligned halfword - can write directly, no RMW
                            if (is_low_halfword_aligned) begin
                                state <= WRITE_LOW_SETUP;  // Just write LOW
                            end else begin
                                state <= WRITE_HIGH_SETUP; // Just write HIGH
                            end
                        end else begin
                            // Partial write (byte or unaligned) - need RMW
                            state <= RMW_READ_LOW_SETUP;
                        end
                    end
                end

                //==============================================================
                // FULL READ: 32-bit Read Operation (4 cycles)
                //==============================================================
                READ_LOW_SETUP: begin
                    // Cycle 1: Setup LOW halfword read
                    sram_addr <= sram_addr_low;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b0;
                    sram_we_n <= 1'b1;
                    data_oe <= 1'b0;
                    state <= READ_LOW_CAPTURE;
                end

                READ_LOW_CAPTURE: begin
                    // Cycle 2: Capture LOW halfword (tAA = 10ns < 20ns ✓)
                    rdata_low <= sram_data;
                    state <= READ_HIGH_SETUP;
                end

                READ_HIGH_SETUP: begin
                    // Cycle 3: Setup HIGH halfword read
                    sram_addr <= sram_addr_high;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b0;
                    sram_we_n <= 1'b1;
                    data_oe <= 1'b0;
                    state <= READ_HIGH_CAPTURE;
                end

                READ_HIGH_CAPTURE: begin
                    // Cycle 4: Capture HIGH halfword and complete
                    rdata_high <= sram_data;
                    rdata <= {sram_data, rdata_low};  // Assemble 32-bit result

                    // Deassert SRAM signals
                    sram_cs_n <= 1'b1;
                    sram_oe_n <= 1'b1;

                    // Signal completion
                    ready <= 1'b1;
                    state <= IDLE;
                end

                //==============================================================
                // FULL WRITE: 32-bit Write Operation (4 cycles)
                //==============================================================
                WRITE_LOW_SETUP: begin
                    // Cycle 1: Setup LOW halfword write
                    sram_addr <= sram_addr_low;
                    data_out_reg <= wdata_reg[15:0];
                    data_oe <= 1'b1;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b1;  // OE must be high during write
                    sram_we_n <= 1'b1;  // WE high during setup
                    state <= WRITE_LOW_PULSE;
                    // synthesis translate_off
                    $display("[SRAM_UNIFIED] WRITE_LOW_SETUP: addr=0x%05x data=0x%04x", sram_addr_low, wdata_reg[15:0]);
                    // synthesis translate_on
                end

                WRITE_LOW_PULSE: begin
                    // Cycle 2: Pulse WE for LOW halfword (20ns > 7ns tWP ✓)
                    sram_we_n <= 1'b0;
                    // Keep WE low entire cycle

                    // synthesis translate_off
                    $display("[SRAM_UNIFIED] WRITE_LOW_PULSE: WE=0, CS=0, addr=0x%05x", sram_addr);
                    // synthesis translate_on

                    // Always go to COMPLETE state to deassert WE cleanly
                    state <= WRITE_LOW_COMPLETE;
                end

                WRITE_LOW_COMPLETE: begin
                    // Cycle 3: Deassert WE (completes LOW write)
                    sram_we_n <= 1'b1;
                    // Address/data still stable from SETUP state

                    // synthesis translate_off
                    $display("[SRAM_UNIFIED] WRITE_LOW_COMPLETE: WE=1, completing LOW write");
                    // synthesis translate_on

                    // Check if we need to write HIGH halfword too
                    if (is_full_write) begin
                        state <= WRITE_HIGH_SETUP;
                    end else if (is_low_halfword_aligned) begin
                        // Only LOW halfword - done
                        ready <= 1'b1;
                        state <= IDLE;
                    end else begin
                        // Should not reach here
                        ready <= 1'b1;
                        state <= IDLE;
                    end
                end

                WRITE_HIGH_SETUP: begin
                    // Cycle 3: Setup HIGH halfword write
                    sram_addr <= sram_addr_high;
                    data_out_reg <= wdata_reg[31:16];
                    data_oe <= 1'b1;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b1;
                    sram_we_n <= 1'b1;  // WE high during setup
                    state <= WRITE_HIGH_PULSE;
                    // synthesis translate_off
                    $display("[SRAM_UNIFIED] WRITE_HIGH_SETUP: addr=0x%05x data=0x%04x, WE rising", sram_addr_high, wdata_reg[31:16]);
                    // synthesis translate_on
                end

                WRITE_HIGH_PULSE: begin
                    // Cycle 5: Pulse WE for HIGH halfword (hold low entire cycle)
                    sram_we_n <= 1'b0;
                    // Keep WE low entire cycle

                    // synthesis translate_off
                    $display("[SRAM_UNIFIED] WRITE_HIGH_PULSE: WE=0, CS=0, addr=0x%05x", sram_addr);
                    // synthesis translate_on

                    // Go to complete state
                    state <= WRITE_HIGH_COMPLETE;
                end

                WRITE_HIGH_COMPLETE: begin
                    // Cycle 6: Deassert WE (completes HIGH write)
                    sram_we_n <= 1'b1;

                    // synthesis translate_off
                    $display("[SRAM_UNIFIED] WRITE_HIGH_COMPLETE: WE=1, completing HIGH write");
                    // synthesis translate_on

                    // Signal completion and return to IDLE
                    ready <= 1'b1;
                    state <= IDLE;
                end

                //==============================================================
                // READ-MODIFY-WRITE: Partial Write Operation (7-9 cycles)
                //==============================================================
                RMW_READ_LOW_SETUP: begin
                    // Cycle 1: Setup LOW halfword read
                    sram_addr <= sram_addr_low;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b0;
                    sram_we_n <= 1'b1;
                    data_oe <= 1'b0;
                    state <= RMW_READ_LOW_CAPTURE;
                end

                RMW_READ_LOW_CAPTURE: begin
                    // Cycle 2: Capture LOW halfword
                    rdata_low <= sram_data;
                    state <= RMW_READ_HIGH_SETUP;
                end

                RMW_READ_HIGH_SETUP: begin
                    // Cycle 3: Setup HIGH halfword read
                    sram_addr <= sram_addr_high;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b0;
                    sram_we_n <= 1'b1;
                    data_oe <= 1'b0;
                    state <= RMW_READ_HIGH_CAPTURE;
                end

                RMW_READ_HIGH_CAPTURE: begin
                    // Cycle 4: Capture HIGH halfword
                    rdata_high <= sram_data;

                    // Deassert read signals
                    sram_cs_n <= 1'b1;
                    sram_oe_n <= 1'b1;

                    // Merge happens combinationally (merged_low/merged_high)
                    // Decide which halfword(s) to write back
                    if (low_halfword_affected) begin
                        state <= RMW_WRITE_LOW_SETUP;
                    end else if (high_halfword_affected) begin
                        state <= RMW_WRITE_HIGH_SETUP;  // Use RMW-specific HIGH write
                    end else begin
                        // No bytes selected? Should not happen
                        ready <= 1'b1;
                        state <= IDLE;
                    end
                end

                RMW_WRITE_LOW_SETUP: begin
                    // Cycle 5: Setup LOW halfword write (merged data)
                    sram_addr <= sram_addr_low;
                    data_out_reg <= merged_low;
                    data_oe <= 1'b1;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b1;
                    sram_we_n <= 1'b1;
                    state <= RMW_WRITE_LOW_PULSE;
                end

                RMW_WRITE_LOW_PULSE: begin
                    // Cycle 6: Pulse WE for LOW halfword
                    sram_we_n <= 1'b0;
                    // Keep WE low entire cycle

                    // Go to complete state
                    state <= RMW_WRITE_LOW_COMPLETE;
                end

                RMW_WRITE_LOW_COMPLETE: begin
                    // Cycle 7: Deassert WE (completes LOW write)
                    sram_we_n <= 1'b1;

                    // Check if HIGH halfword also affected
                    if (high_halfword_affected) begin
                        state <= RMW_WRITE_HIGH_SETUP;  // Use RMW-specific HIGH write
                    end else begin
                        // Only LOW halfword - done
                        ready <= 1'b1;
                        state <= IDLE;
                    end
                end

                //==============================================================
                // RMW HIGH Write States (for byte/halfword in HIGH halfword)
                //==============================================================
                RMW_WRITE_HIGH_SETUP: begin
                    // Setup HIGH halfword write with merged data
                    sram_addr <= sram_addr_high;
                    data_out_reg <= merged_high;  // Use merged data, not wdata_reg!
                    data_oe <= 1'b1;
                    sram_cs_n <= 1'b0;
                    sram_oe_n <= 1'b1;
                    sram_we_n <= 1'b1;
                    state <= RMW_WRITE_HIGH_PULSE;
                end

                RMW_WRITE_HIGH_PULSE: begin
                    // Pulse WE for HIGH halfword
                    sram_we_n <= 1'b0;
                    state <= RMW_WRITE_HIGH_COMPLETE;
                end

                RMW_WRITE_HIGH_COMPLETE: begin
                    // Deassert WE (completes HIGH write)
                    sram_we_n <= 1'b1;
                    ready <= 1'b1;
                    state <= IDLE;
                end

                //==============================================================
                // Default: Return to IDLE
                //==============================================================
                default: begin
                    state <= IDLE;
                end
            endcase
        end
    end

    //==========================================================================
    // Simulation/Debug Support
    //==========================================================================
    // synthesis translate_off
    always @(posedge clk) begin
        if (valid && !ready && state == IDLE) begin
            $display("[SRAM_UNIFIED] START: addr=0x%08x wdata=0x%08x wstrb=0x%x",
                     addr, wdata, wstrb);
        end
        if (ready) begin
            if (is_read) begin
                $display("[SRAM_UNIFIED] COMPLETE READ: addr=0x%08x rdata=0x%08x",
                         addr_reg, rdata);
            end else begin
                $display("[SRAM_UNIFIED] COMPLETE WRITE: addr=0x%08x wdata=0x%08x wstrb=0x%x",
                         addr_reg, wdata_reg, wstrb_reg);
            end
        end
    end
    // synthesis translate_on

endmodule
