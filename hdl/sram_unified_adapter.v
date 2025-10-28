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
//==============================================================================

module sram_unified_adapter (
    input wire clk,
    input wire resetn,

    // mem_controller Interface (start/busy/done style)
    input wire start,
    input wire [7:0] cmd,            // Command byte (unused - wstrb determines operation)
    input wire [31:0] addr_in,
    input wire [31:0] data_in,
    input wire [3:0] mem_wstrb,
    output reg busy,
    output reg done,
    output reg [31:0] result,

    // SRAM Physical Interface (passed through to unified controller)
    output wire [17:0] sram_addr,
    inout wire [15:0] sram_data,
    output wire sram_cs_n,
    output wire sram_oe_n,
    output wire sram_we_n
);

    //==========================================================================
    // Protocol Conversion: start/busy/done → valid/ready
    //==========================================================================
    reg valid_reg;
    wire ready_wire;
    wire [31:0] rdata_wire;

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
                    // Wait for ready from unified controller
                    if (ready_wire) begin
                        // Transaction complete
                        valid_reg <= 1'b0;
                        result <= rdata_wire;
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
    // Unified SRAM Controller Instantiation
    //==========================================================================
    sram_controller_unified unified_ctrl (
        .clk(clk),
        .resetn(resetn),

        // CPU Interface (valid/ready)
        .valid(valid_reg),
        .ready(ready_wire),
        .wstrb(mem_wstrb),
        .addr(addr_in),
        .wdata(data_in),
        .rdata(rdata_wire),

        // SRAM Physical Interface (16-bit)
        .sram_addr(sram_addr),
        .sram_data(sram_data),
        .sram_cs_n(sram_cs_n),
        .sram_oe_n(sram_oe_n),
        .sram_we_n(sram_we_n)
    );

endmodule
