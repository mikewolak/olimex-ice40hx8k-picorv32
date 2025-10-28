//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// sram_unified_adapter.v - Adapter for Unified SRAM Controller
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//
//==============================================================================
// PURPOSE: Adapts the unified SRAM controller (valid/ready interface) to
//          match the existing mem_controller expectations (start/busy/done)
//
// This allows drop-in replacement without modifying mem_controller.v
//
// DUAL-CLOCK DESIGN:
// - CPU side (clk): 50MHz - mem_controller interface
// - SRAM side (sram_clk): 100MHz - unified controller for 2x faster access
// - Clock Domain Crossing: 2-FF synchronizers for handshake signals
//==============================================================================

module sram_unified_adapter (
    input wire clk,              // 50MHz CPU clock
    input wire sram_clk,          // 100MHz SRAM clock
    input wire resetn,

    // mem_controller Interface (start/busy/done style) - 50MHz domain
    input wire start,
    input wire [7:0] cmd,            // Command byte (unused - wstrb determines operation)
    input wire [31:0] addr_in,
    input wire [31:0] data_in,
    input wire [3:0] mem_wstrb,
    output reg busy,
    output reg done,
    output reg [31:0] result,

    // SRAM Physical Interface (passed through to unified controller) - 100MHz domain
    output wire [17:0] sram_addr,
    inout wire [15:0] sram_data,
    output wire sram_cs_n,
    output wire sram_oe_n,
    output wire sram_we_n
);

    //==========================================================================
    // Protocol Conversion: start/busy/done → valid/ready (50MHz domain)
    //==========================================================================
    reg valid_reg;
    wire ready_wire;              // From 100MHz controller
    wire [31:0] rdata_wire;       // From 100MHz controller

    //==========================================================================
    // Clock Domain Crossing - 2-FF Synchronizers
    //==========================================================================
    // Sync valid from 50MHz → 100MHz
    reg valid_sync1, valid_sync2;
    always @(posedge sram_clk or negedge resetn) begin
        if (!resetn) begin
            valid_sync1 <= 1'b0;
            valid_sync2 <= 1'b0;
        end else begin
            valid_sync1 <= valid_reg;
            valid_sync2 <= valid_sync1;
        end
    end

    // Sync ready from 100MHz → 50MHz
    reg ready_sync1, ready_sync2;
    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            ready_sync1 <= 1'b0;
            ready_sync2 <= 1'b0;
        end else begin
            ready_sync1 <= ready_wire;
            ready_sync2 <= ready_sync1;
        end
    end

    wire ready_synced = ready_sync2;  // Synchronized to 50MHz

    // State machine for handshake conversion
    localparam IDLE = 2'd0;
    localparam ACTIVE = 2'd1;
    localparam COMPLETING = 2'd2;

    reg [1:0] state;

    always @(posedge clk) begin
        if (!resetn) begin
            state <= IDLE;
            valid_reg <= 1'b0;
            busy <= 1'b0;
            done <= 1'b0;
            result <= 32'h0;
        end else begin
            case (state)
                IDLE: begin
                    done <= 1'b0;
                    valid_reg <= 1'b0;

                    if (start && !busy) begin
                        // Start received - assert valid and busy
                        valid_reg <= 1'b1;
                        busy <= 1'b1;
                        state <= ACTIVE;
                    end
                end

                ACTIVE: begin
                    // Wait for synchronized ready from unified controller (100MHz → 50MHz)
                    if (ready_synced) begin
                        // Transaction complete
                        valid_reg <= 1'b0;
                        result <= rdata_wire;  // Safe: rdata is stable after ready
                        done <= 1'b1;
                        busy <= 1'b0;
                        state <= COMPLETING;
                    end
                end

                COMPLETING: begin
                    // Hold done for one cycle
                    done <= 1'b0;
                    state <= IDLE;
                end

                default: state <= IDLE;
            endcase
        end
    end

    //==========================================================================
    // Unified SRAM Controller Instantiation (100MHz domain)
    //==========================================================================
    sram_controller_unified unified_ctrl (
        .clk(sram_clk),           // 100MHz clock for 2x faster access
        .resetn(resetn),

        // CPU Interface (valid/ready) - synchronized from 50MHz
        .valid(valid_sync2),      // Synchronized valid from 50MHz domain
        .ready(ready_wire),       // Will be synchronized back to 50MHz
        .wstrb(mem_wstrb),        // Static during transaction (safe)
        .addr(addr_in),           // Static during transaction (safe)
        .wdata(data_in),          // Static during transaction (safe)
        .rdata(rdata_wire),       // Latched, stable after ready (safe)

        // SRAM Physical Interface (16-bit) - 100MHz domain
        .sram_addr(sram_addr),
        .sram_data(sram_data),
        .sram_cs_n(sram_cs_n),
        .sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n)
    );

endmodule
