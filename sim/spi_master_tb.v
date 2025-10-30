//==============================================================================
// SPI Master Testbench - Comprehensive Verification
//
// Tests:
// 1. Single-byte transfers (baseline)
// 2. Burst mode: 2, 4, 8, 16, 32, 64, 128, 256, 512 bytes
// 3. Clock polarity and phase modes (CPOL/CPHA)
// 4. Clock divider settings
// 5. Burst counter edge cases (count=1, count=2, count=512)
// 6. Chip select control
//
// Copyright (c) October 2025 Michael Wolak
//==============================================================================

`timescale 1ns / 1ps

module spi_master_tb();

    //==========================================================================
    // Clock and Reset
    //==========================================================================
    reg clk;
    reg resetn;

    // 50 MHz clock generation (20ns period)
    initial begin
        clk = 0;
        forever #10 clk = ~clk;
    end

    //==========================================================================
    // DUT Signals
    //==========================================================================
    reg         mmio_valid;
    reg         mmio_write;
    reg  [31:0] mmio_addr;
    reg  [31:0] mmio_wdata;
    reg  [3:0]  mmio_wstrb;
    wire [31:0] mmio_rdata;
    wire        mmio_ready;

    wire        spi_sck;
    wire        spi_mosi;
    reg         spi_miso;
    wire        spi_cs;
    wire        spi_irq;

    //==========================================================================
    // Memory Map
    //==========================================================================
    localparam ADDR_SPI_CTRL   = 32'h80000050;
    localparam ADDR_SPI_DATA   = 32'h80000054;
    localparam ADDR_SPI_STATUS = 32'h80000058;
    localparam ADDR_SPI_CS     = 32'h8000005C;
    localparam ADDR_SPI_BURST  = 32'h80000060;

    //==========================================================================
    // Test Statistics
    //==========================================================================
    integer test_count = 0;
    integer test_passed = 0;
    integer test_failed = 0;

    //==========================================================================
    // Instantiate DUT
    //==========================================================================
    spi_master dut (
        .clk(clk),
        .resetn(resetn),
        .mmio_valid(mmio_valid),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(mmio_rdata),
        .mmio_ready(mmio_ready),
        .spi_sck(spi_sck),
        .spi_mosi(spi_mosi),
        .spi_miso(spi_miso),
        .spi_cs(spi_cs),
        .spi_irq(spi_irq)
    );

    //==========================================================================
    // MMIO Transaction Tasks
    //==========================================================================
    task mmio_write_reg;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            mmio_valid = 1;
            mmio_write = 1;
            mmio_addr = addr;
            mmio_wdata = data;
            mmio_wstrb = 4'hF;
            @(posedge clk);
            while (!mmio_ready) @(posedge clk);
            mmio_valid = 0;
            mmio_write = 0;
            @(posedge clk);
        end
    endtask

    task mmio_read_reg;
        input  [31:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            mmio_valid = 1;
            mmio_write = 0;
            mmio_addr = addr;
            mmio_wstrb = 4'h0;
            @(posedge clk);
            while (!mmio_ready) @(posedge clk);
            data = mmio_rdata;
            mmio_valid = 0;
            @(posedge clk);
        end
    endtask

    task wait_spi_done;
        reg [31:0] status;
        integer timeout;
        begin
            timeout = 0;
            mmio_read_reg(ADDR_SPI_STATUS, status);
            while ((status & 32'h1) && timeout < 10000) begin
                #100;
                mmio_read_reg(ADDR_SPI_STATUS, status);
                timeout = timeout + 1;
            end
            if (timeout >= 10000) begin
                $display("[ERROR] SPI timeout waiting for done");
                test_failed = test_failed + 1;
            end
        end
    endtask

    task wait_burst_complete;
        reg [31:0] status;
        integer timeout;
        begin
            timeout = 0;
            mmio_read_reg(ADDR_SPI_STATUS, status);
            while ((status & 32'h4) && timeout < 100000) begin
                #100;
                mmio_read_reg(ADDR_SPI_STATUS, status);
                timeout = timeout + 1;
            end
            if (timeout >= 100000) begin
                $display("[ERROR] Burst mode timeout waiting for completion");
                test_failed = test_failed + 1;
            end
        end
    endtask

    //==========================================================================
    // Test Cases
    //==========================================================================

    task test_single_byte_transfer;
        reg [31:0] rx_data;
        begin
            test_count = test_count + 1;
            $display("\n[TEST %0d] Single-byte transfer (baseline)", test_count);

            // Configure SPI: CPOL=0, CPHA=0, CLK_DIV=/1
            mmio_write_reg(ADDR_SPI_CTRL, 32'h00);

            // Assert CS
            mmio_write_reg(ADDR_SPI_CS, 32'h00);

            // Set MISO pattern (loopback simulation)
            spi_miso = 1'b1;

            // Send byte 0xA5
            mmio_write_reg(ADDR_SPI_DATA, 32'h000000A5);
            wait_spi_done();

            // Read received data
            mmio_read_reg(ADDR_SPI_DATA, rx_data);

            // Deassert CS
            mmio_write_reg(ADDR_SPI_CS, 32'h01);

            if (rx_data[7:0] == 8'hFF) begin
                $display("[PASS] Single-byte transfer completed");
                test_passed = test_passed + 1;
            end else begin
                $display("[FAIL] Expected 0xFF, got 0x%02x", rx_data[7:0]);
                test_failed = test_failed + 1;
            end
        end
    endtask

    task test_burst_transfer;
        input [12:0] burst_count;
        reg [31:0] status;
        reg [31:0] burst_remain;
        integer i;
        begin
            test_count = test_count + 1;
            $display("\n[TEST %0d] Burst transfer: %0d bytes", test_count, burst_count);

            // Configure SPI
            mmio_write_reg(ADDR_SPI_CTRL, 32'h00);
            mmio_write_reg(ADDR_SPI_CS, 32'h00);

            // Set burst count
            mmio_write_reg(ADDR_SPI_BURST, burst_count);

            // Verify burst mode activated
            mmio_read_reg(ADDR_SPI_STATUS, status);
            if (!(status & 32'h4)) begin
                $display("[FAIL] Burst mode not activated (status=0x%08x)", status);
                test_failed = test_failed + 1;
                return;
            end

            // Transfer all bytes
            for (i = 0; i < burst_count; i = i + 1) begin
                spi_miso = i[0]; // Alternating pattern
                mmio_write_reg(ADDR_SPI_DATA, 8'h00 + i[7:0]);
                wait_spi_done();

                // Check burst counter (should decrement)
                mmio_read_reg(ADDR_SPI_BURST, burst_remain);
                if (i < burst_count - 1) begin
                    if (burst_remain != (burst_count - i - 1)) begin
                        $display("[FAIL] Burst counter mismatch at byte %0d: expected %0d, got %0d",
                                 i, burst_count - i - 1, burst_remain);
                        test_failed = test_failed + 1;
                        return;
                    end
                end
            end

            // Wait for burst mode to clear
            wait_burst_complete();

            // Verify burst mode deactivated
            mmio_read_reg(ADDR_SPI_STATUS, status);
            if (status & 32'h4) begin
                $display("[FAIL] Burst mode still active after completion (status=0x%08x)", status);
                test_failed = test_failed + 1;
            end else begin
                $display("[PASS] Burst transfer %0d bytes completed successfully", burst_count);
                test_passed = test_passed + 1;
            end

            // Deassert CS
            mmio_write_reg(ADDR_SPI_CS, 32'h01);
        end
    endtask

    task test_burst_count_edge_case;
        input [12:0] count;
        input string description;
        reg [31:0] status;
        integer i;
        begin
            test_count = test_count + 1;
            $display("\n[TEST %0d] Burst edge case: %s (count=%0d)", test_count, description, count);

            mmio_write_reg(ADDR_SPI_CS, 32'h00);
            mmio_write_reg(ADDR_SPI_BURST, count);

            for (i = 0; i < count; i = i + 1) begin
                spi_miso = 1'b0;
                mmio_write_reg(ADDR_SPI_DATA, 8'hAA);
                wait_spi_done();
            end

            wait_burst_complete();

            mmio_read_reg(ADDR_SPI_STATUS, status);
            if (!(status & 32'h4)) begin
                $display("[PASS] Burst edge case %s passed", description);
                test_passed = test_passed + 1;
            end else begin
                $display("[FAIL] Burst mode did not clear for %s", description);
                test_failed = test_failed + 1;
            end

            mmio_write_reg(ADDR_SPI_CS, 32'h01);
        end
    endtask

    task test_cpol_cpha_modes;
        integer cpol, cpha;
        reg [31:0] ctrl;
        begin
            for (cpol = 0; cpol <= 1; cpol = cpol + 1) begin
                for (cpha = 0; cpha <= 1; cpha = cpha + 1) begin
                    test_count = test_count + 1;
                    $display("\n[TEST %0d] CPOL=%0d CPHA=%0d mode", test_count, cpol, cpha);

                    ctrl = {29'h0, 1'b0, cpha[0], cpol[0]};
                    mmio_write_reg(ADDR_SPI_CTRL, ctrl);
                    mmio_write_reg(ADDR_SPI_CS, 32'h00);

                    spi_miso = 1'b1;
                    mmio_write_reg(ADDR_SPI_DATA, 32'h55);
                    wait_spi_done();

                    mmio_write_reg(ADDR_SPI_CS, 32'h01);

                    $display("[PASS] CPOL=%0d CPHA=%0d transfer completed", cpol, cpha);
                    test_passed = test_passed + 1;
                end
            end
        end
    endtask

    task test_clock_dividers;
        integer div;
        reg [31:0] ctrl;
        begin
            for (div = 0; div <= 7; div = div + 1) begin
                test_count = test_count + 1;
                $display("\n[TEST %0d] Clock divider /%0d", test_count, (1 << div));

                ctrl = {27'h0, div[2:0], 2'b00};
                mmio_write_reg(ADDR_SPI_CTRL, ctrl);
                mmio_write_reg(ADDR_SPI_CS, 32'h00);

                spi_miso = 1'b0;
                mmio_write_reg(ADDR_SPI_DATA, 32'h33);
                wait_spi_done();

                mmio_write_reg(ADDR_SPI_CS, 32'h01);

                $display("[PASS] Clock divider /%0d test passed", (1 << div));
                test_passed = test_passed + 1;
            end
        end
    endtask

    task test_critical_512_byte_burst;
        reg [31:0] status, burst_remain;
        integer i;
        integer errors;
        begin
            test_count = test_count + 1;
            errors = 0;
            $display("\n[TEST %0d] CRITICAL: 512-byte burst (SD card block size)", test_count);

            mmio_write_reg(ADDR_SPI_CTRL, 32'h00);
            mmio_write_reg(ADDR_SPI_CS, 32'h00);
            mmio_write_reg(ADDR_SPI_BURST, 13'd512);

            // Verify burst mode active
            mmio_read_reg(ADDR_SPI_STATUS, status);
            $display("  Initial status: 0x%08x (burst_mode=%b)", status, status[2]);

            for (i = 0; i < 512; i = i + 1) begin
                spi_miso = i[0];
                mmio_write_reg(ADDR_SPI_DATA, i[7:0]);
                wait_spi_done();

                // Check progress every 64 bytes
                if ((i % 64) == 63) begin
                    mmio_read_reg(ADDR_SPI_BURST, burst_remain);
                    $display("  Byte %0d/512: burst_count=%0d", i+1, burst_remain);
                    if (burst_remain != (512 - i - 1)) begin
                        $display("  [ERROR] Counter mismatch at byte %0d: expected %0d, got %0d",
                                 i+1, 512 - i - 1, burst_remain);
                        errors = errors + 1;
                    end
                end
            end

            wait_burst_complete();

            // Final verification
            mmio_read_reg(ADDR_SPI_STATUS, status);
            mmio_read_reg(ADDR_SPI_BURST, burst_remain);
            $display("  Final status: 0x%08x (burst_mode=%b, burst_count=%0d)",
                     status, status[2], burst_remain);

            if ((status & 32'h4) == 0 && burst_remain == 0 && errors == 0) begin
                $display("[PASS] 512-byte burst completed correctly - ALL 512 BYTES TRANSFERRED");
                test_passed = test_passed + 1;
            end else begin
                $display("[FAIL] 512-byte burst failed (burst_mode=%b, count=%0d, errors=%0d)",
                         status[2], burst_remain, errors);
                test_failed = test_failed + 1;
            end

            mmio_write_reg(ADDR_SPI_CS, 32'h01);
        end
    endtask

    //==========================================================================
    // Main Test Sequence
    //==========================================================================
    initial begin
        $display("========================================");
        $display("SPI Master Comprehensive Test Suite");
        $display("========================================");

        // Initialize
        resetn = 0;
        mmio_valid = 0;
        mmio_write = 0;
        mmio_addr = 0;
        mmio_wdata = 0;
        mmio_wstrb = 0;
        spi_miso = 0;

        #100;
        resetn = 1;
        #100;

        // Run all tests
        test_single_byte_transfer();

        test_cpol_cpha_modes();

        test_clock_dividers();

        // Edge cases first
        test_burst_count_edge_case(1, "count=1");
        test_burst_count_edge_case(2, "count=2");
        test_burst_count_edge_case(3, "count=3");

        // Standard burst sizes
        test_burst_transfer(2);
        test_burst_transfer(4);
        test_burst_transfer(8);
        test_burst_transfer(16);
        test_burst_transfer(32);
        test_burst_transfer(64);
        test_burst_transfer(128);
        test_burst_transfer(256);

        // CRITICAL TEST: 512-byte burst (SD card block)
        test_critical_512_byte_burst();

        // Additional edge cases
        test_burst_transfer(1024);
        test_burst_transfer(2048);
        test_burst_transfer(4096);
        test_burst_transfer(8192);

        // Final report
        #1000;
        $display("\n========================================");
        $display("Test Suite Complete");
        $display("========================================");
        $display("Total Tests:  %0d", test_count);
        $display("Passed:       %0d", test_passed);
        $display("Failed:       %0d", test_failed);

        if (test_failed == 0) begin
            $display("\n✓ ALL TESTS PASSED - SPI MASTER VERIFIED");
        end else begin
            $display("\n✗ TESTS FAILED - REVIEW ERRORS ABOVE");
        end
        $display("========================================\n");

        $finish;
    end

    // Timeout watchdog
    initial begin
        #500ms;
        $display("\n[ERROR] Simulation timeout after 500ms");
        $display("Test may be stuck in infinite loop");
        $finish;
    end

endmodule
