//==============================================================================
// SRAM Behavioral Model - IS61WV51216BLL-10TLI
//
// 512KB (256K x 16-bit) SRAM Simulation Model
// Access time: 10ns (for -10 speed grade)
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

`timescale 1ns/1ns

module sram_model #(
    parameter ADDR_WIDTH = 18,           // 18-bit address (256K words)
    parameter DATA_WIDTH = 16,           // 16-bit data
    parameter ACCESS_TIME_NS = 10,       // tAA - address to data valid
    parameter WE_PULSE_MIN_NS = 7,       // tWP - write pulse width minimum
    parameter VERBOSE = 1                // Enable debug messages
) (
    input wire [ADDR_WIDTH-1:0] addr,
    inout wire [DATA_WIDTH-1:0] data,
    input wire cs_n,                     // Chip select (active low)
    input wire oe_n,                     // Output enable (active low)
    input wire we_n                      // Write enable (active low)
);

    //==========================================================================
    // Memory Array - 512KB total
    //==========================================================================
    reg [DATA_WIDTH-1:0] memory [0:(1 << ADDR_WIDTH)-1];

    // Initialize memory to known pattern for debugging
    integer i;
    initial begin
        for (i = 0; i < (1 << ADDR_WIDTH); i = i + 1) begin
            memory[i] = 16'h0000;  // Start with zeros
        end

        // Load firmware if file exists
        if ($test$plusargs("FIRMWARE")) begin
            $readmemh("../firmware/led_blink.hex", memory);
            $display("[SRAM_MODEL] Loaded firmware from ../firmware/led_blink.hex");
        end

        if (VERBOSE) begin
            $display("[SRAM_MODEL] Initialized %0d KB memory (256K x 16-bit)",
                     (1 << ADDR_WIDTH) * 2 / 1024);
            $display("[SRAM_MODEL] First instructions: 0x%04x 0x%04x 0x%04x 0x%04x",
                     memory[0], memory[1], memory[2], memory[3]);
        end
    end

    //==========================================================================
    // Read Operation
    //==========================================================================
    reg [DATA_WIDTH-1:0] data_out;
    reg data_out_enable;

    // Tri-state output control
    assign data = data_out_enable ? data_out : {DATA_WIDTH{1'bz}};

    // Read: CS low, OE low, WE high
    wire read_enable = (!cs_n) && (!oe_n) && (we_n);

    always @(*) begin
        if (read_enable) begin
            // Simulate access time delay
            data_out <= #ACCESS_TIME_NS memory[addr];
            data_out_enable = 1'b1;

            if (VERBOSE) begin
                $display("[SRAM_MODEL] READ  addr=0x%05x data=0x%04x @ %0t",
                         addr, memory[addr], $time);
            end
        end else begin
            data_out = {DATA_WIDTH{1'bz}};
            data_out_enable = 1'b0;
        end
    end

    //==========================================================================
    // Write Operation
    //==========================================================================
    // Write: CS low, WE low (OE should be high, but not critical)
    wire write_enable = (!cs_n) && (!we_n);

    // Track write pulse timing
    time we_fall_time;
    time we_rise_time;
    real we_pulse_width;

    // Detect WE falling edge
    always @(negedge we_n) begin
        if (!cs_n) begin
            we_fall_time = $time;
        end
    end

    // Perform write on WE rising edge
    always @(posedge we_n) begin
        if (!cs_n) begin
            we_rise_time = $time;
            we_pulse_width = (we_rise_time - we_fall_time) / 1000.0; // Convert to ns

            // Check timing
            if (we_pulse_width < WE_PULSE_MIN_NS) begin
                $display("[SRAM_MODEL] WARNING: Write pulse too short! " +
                         "Width=%.1fns, Min=%0dns @ %0t",
                         we_pulse_width, WE_PULSE_MIN_NS, $time);
            end

            // Perform the write
            memory[addr] <= data;

            if (VERBOSE) begin
                $display("[SRAM_MODEL] WRITE addr=0x%05x data=0x%04x (pulse=%.1fns) @ %0t",
                         addr, data, we_pulse_width, $time);
            end
        end
    end

    //==========================================================================
    // Violation Checks
    //==========================================================================
    always @(*) begin
        // Check for bus contention (both WE and OE active)
        if (!cs_n && !we_n && !oe_n) begin
            $display("[SRAM_MODEL] ERROR: Bus contention! WE and OE both active @ %0t",
                     $time);
        end
    end

    //==========================================================================
    // Debug Tasks
    //==========================================================================
    // Task to load memory from file (hex format)
    task load_mem;
        input [1024*8-1:0] filename;
        begin
            $readmemh(filename, memory);
            $display("[SRAM_MODEL] Loaded memory from %s", filename);
        end
    endtask

    // Task to dump memory region
    task dump_mem;
        input [ADDR_WIDTH-1:0] start_addr;
        input [ADDR_WIDTH-1:0] end_addr;
        integer j;
        begin
            $display("[SRAM_MODEL] Memory dump [0x%05x:0x%05x]:", start_addr, end_addr);
            for (j = start_addr; j <= end_addr; j = j + 1) begin
                $display("  [0x%05x] = 0x%04x", j, memory[j]);
            end
        end
    endtask

endmodule
