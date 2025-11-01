//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// bootloader_rom.v - Bootloader ROM (SPRAM/Generic)
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * Bootloader ROM - 8KB at 0x40000
 *
 * READ-ONLY BRAM initialized from bootloader.hex at synthesis time
 *
 * This uses iCE40 BRAM (not SPRAM) which CAN be initialized during
 * synthesis via $readmemh. Yosys will infer this as SB_RAM40_4K blocks.
 *
 * Memory map:
 *   0x00000000 - 0x0003FFFF : Main firmware (256KB SRAM)
 *   0x00040000 - 0x00041FFF : Bootloader ROM (8KB BRAM) ← THIS MODULE
 *   0x00042000 - 0x0007FFFF : Heap/Stack (~248KB SRAM)
 *
 * Interface:
 *   - 32-bit read-only interface
 *   - Single cycle latency (registered output)
 *   - No write capability (true ROM)
 */

`default_nettype none

module bootloader_rom (
    input  wire        clk,
    input  wire        resetn,
    input  wire [12:0] addr,        // 8KB = 2^13 bytes, 2^11 words
    input  wire        enable,
    output reg  [31:0] rdata
);

    // Memory declaration - 2048 x 32-bit words = 8KB
    // Yosys will infer this as BRAM (SB_RAM40_4K blocks)
    (* ram_style = "block" *) reg [31:0] memory [0:2047];

    // Initialize memory from selected bootloader at synthesis time
    // The Makefile creates a symlink bootloader.hex.selected -> chosen bootloader
    // Yosys supports $readmemh for BRAM initialization
    initial begin
        `ifdef SIMULATION
            // For simulation, use original UART bootloader (backward compatible)
            $readmemh("../bootloader/bootloader.hex", memory);
            $display("[BOOTROM] Loaded bootloader.hex for simulation");
        `else
            // For synthesis, use selected bootloader (via symlink)
            $readmemh("bootloader/bootloader.hex.selected", memory);
        `endif
    end

    // Read-only logic
    always @(posedge clk) begin
        if (!resetn) begin
            rdata <= 32'h0;
        end else if (enable) begin
            rdata <= memory[addr[12:2]];
        end
    end

endmodule

`default_nettype wire
