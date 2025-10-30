//==============================================================================
// SPI Master MMIO Integration Testbench
//
// Tests complete memory-mapped I/O path including:
// - Address decoder routing
// - Burst register access
// - Full 512-byte burst transfer sequence
//==============================================================================

`timescale 1ns/1ps

module spi_mmio_tb;

    // Clock and reset
    reg clk;
    reg resetn;

    // Memory-mapped I/O bus (from CPU perspective)
    reg         mmio_valid;
    reg         mmio_write;
    reg  [31:0] mmio_addr;
    reg  [31:0] mmio_wdata;
    reg  [3:0]  mmio_wstrb;
    wire [31:0] mmio_rdata;
    wire        mmio_ready;

    // SPI physical interface
    wire spi_sck;
    wire spi_mosi;
    reg  spi_miso;
    wire spi_cs;
    wire spi_irq;

    // Test control
    integer test_count;
    integer test_passed;
    integer test_failed;

    // SPI register addresses (matching firmware hardware.h)
    localparam ADDR_SPI_CTRL   = 32'h80000050;
    localparam ADDR_SPI_DATA   = 32'h80000054;
    localparam ADDR_SPI_STATUS = 32'h80000058;
    localparam ADDR_SPI_CS     = 32'h8000005C;
    localparam ADDR_SPI_BURST  = 32'h80000060;  // NEW - this is what was failing!

    //==========================================================================
    // Address Decoder (matching ice40_picorv32_top.v)
    //==========================================================================
    wire addr_is_spi = (mmio_addr[31:6] == 26'h2000001);  // 0x80000050-0x8000008F

    //==========================================================================
    // Device Under Test - SPI Master
    //==========================================================================
    spi_master dut (
        .clk(clk),
        .resetn(resetn),
        .mmio_valid(mmio_valid && addr_is_spi),  // Only valid when address matches
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
    // Clock Generation
    //==========================================================================
    initial clk = 0;
    always #10 clk = ~clk;  // 50 MHz clock

    //==========================================================================
    // MMIO Bus Access Tasks
    //==========================================================================

    // Write to MMIO register
    task mmio_write_reg;
        input [31:0] addr;
        input [31:0] data;
        begin
            @(posedge clk);
            mmio_valid = 1'b1;
            mmio_write = 1'b1;
            mmio_addr = addr;
            mmio_wdata = data;
            mmio_wstrb = 4'b1111;

            @(posedge clk);
            while (!mmio_ready) @(posedge clk);

            mmio_valid = 1'b0;
            mmio_write = 1'b0;
            @(posedge clk);
        end
    endtask

    // Read from MMIO register
    task mmio_read_reg;
        input  [31:0] addr;
        output [31:0] data;
        begin
            @(posedge clk);
            mmio_valid = 1'b1;
            mmio_write = 1'b0;
            mmio_addr = addr;
            mmio_wstrb = 4'b0000;

            @(posedge clk);
            while (!mmio_ready) @(posedge clk);

            data = mmio_rdata;
            mmio_valid = 1'b0;
            @(posedge clk);
        end
    endtask

    // Wait for SPI transfer complete
    task wait_spi_done;
        reg [31:0] status;
        begin
            repeat (1000) begin
                mmio_read_reg(ADDR_SPI_STATUS, status);
                if ((status & 32'h1) == 0) begin  // BUSY bit cleared
                    return;
                end
            end
            $display("[ERROR] Timeout waiting for SPI completion");
        end
    endtask

    //==========================================================================
    // Test Sequence
    //==========================================================================
    initial begin
        $display("\n========================================");
        $display("SPI Master MMIO Integration Test");
        $display("========================================\n");

        // Initialize
        test_count = 0;
        test_passed = 0;
        test_failed = 0;

        resetn = 0;
        mmio_valid = 0;
        mmio_write = 0;
        mmio_addr = 0;
        mmio_wdata = 0;
        mmio_wstrb = 0;
        spi_miso = 1'b1;

        // Reset
        repeat (5) @(posedge clk);
        resetn = 1;
        repeat (5) @(posedge clk);

        //======================================================================
        // TEST 1: Verify address decoder routes SPI registers correctly
        //======================================================================
        test_count = test_count + 1;
        $display("[TEST %0d] Address decoder - SPI registers 0x50-0x5C", test_count);

        begin
            reg [31:0] test_addr;
            reg [31:0] readback;

            // Test all standard SPI registers
            test_addr = ADDR_SPI_CTRL;
            $display("  Testing address 0x%08x", test_addr);
            $display("    addr[31:5] = 0x%07x", test_addr[31:5]);
            $display("    Expected:    0x%07x", 27'h4000002);
            $display("    Match: %b", (test_addr[31:5] == 27'h4000002));

            mmio_write_reg(test_addr, 32'h12345678);
            mmio_read_reg(test_addr, readback);

            if (mmio_ready) begin
                $display("  ✓ CTRL register (0x%08x) accessible", test_addr);
                test_passed = test_passed + 1;
            end else begin
                $display("  ✗ CTRL register not accessible - mmio_ready never went high");
                test_failed = test_failed + 1;
            end
        end

        //======================================================================
        // TEST 2: CRITICAL - Verify BURST register (0x60) is accessible
        //======================================================================
        test_count = test_count + 1;
        $display("\n[TEST %0d] CRITICAL: Burst register (0x%08x) accessibility", test_count, ADDR_SPI_BURST);

        begin
            reg [31:0] readback;

            // Write to burst register
            $display("  Writing burst count = 512 to 0x%08x", ADDR_SPI_BURST);
            mmio_write_reg(ADDR_SPI_BURST, 32'd512);

            // Read back status to verify burst mode active
            mmio_read_reg(ADDR_SPI_STATUS, readback);
            $display("  Status after burst write: 0x%08x", readback);
            $display("    - BUSY bit (0): %b", readback[0]);
            $display("    - IRQ bit (1): %b", readback[1]);
            $display("    - BURST_MODE bit (2): %b", readback[2]);
            $display("    - Burst count [15:3]: %d", readback[15:3]);

            if (readback[2] == 1'b1) begin
                $display("  ✓ BURST register accessible, burst mode enabled");
                test_passed = test_passed + 1;
            end else begin
                $display("  ✗ BURST register write failed - burst mode not active!");
                test_failed = test_failed + 1;
            end
        end

        //======================================================================
        // TEST 3: Address decoder rejects non-SPI addresses
        //======================================================================
        test_count = test_count + 1;
        $display("\n[TEST %0d] Address decoder - non-SPI address rejection", test_count);

        begin
            reg [31:0] bad_addr;

            bad_addr = 32'h80000070;  // Just outside SPI range
            @(posedge clk);
            mmio_valid = 1'b1;
            mmio_write = 1'b1;
            mmio_addr = bad_addr;
            mmio_wdata = 32'hDEADBEEF;
            mmio_wstrb = 4'b1111;

            @(posedge clk);
            @(posedge clk);

            if (addr_is_spi == 1'b0 && mmio_ready == 1'b0) begin
                $display("  ✓ Address 0x%08x correctly rejected (not routed to SPI)", bad_addr);
                test_passed = test_passed + 1;
            end else begin
                $display("  ✗ Address decoder incorrectly accepted 0x%08x", bad_addr);
                test_failed = test_failed + 1;
            end

            mmio_valid = 1'b0;
            mmio_write = 1'b0;
            @(posedge clk);
        end

        //======================================================================
        // TEST 4: Full burst transfer sequence (like firmware does)
        //======================================================================
        test_count = test_count + 1;
        $display("\n[TEST %0d] Complete 512-byte burst transfer sequence", test_count);

        begin
            integer i;
            reg [31:0] status;
            integer errors;

            errors = 0;

            // Initialize SPI
            $display("  1. Initialize SPI controller");
            mmio_write_reg(ADDR_SPI_CTRL, 32'h00);  // CPOL=0, CPHA=0, CLK_DIV=/1
            mmio_write_reg(ADDR_SPI_CS, 32'h00);    // CS asserted

            // Set burst count
            $display("  2. Set burst count to 512");
            mmio_write_reg(ADDR_SPI_BURST, 32'd512);

            // Verify burst mode active
            mmio_read_reg(ADDR_SPI_STATUS, status);
            if (status[2] != 1'b1) begin
                $display("  ✗ Burst mode not active after setting count!");
                errors = errors + 1;
            end else begin
                $display("  ✓ Burst mode active, count = %d", status[15:3]);
            end

            // Transfer 512 bytes
            $display("  3. Transferring 512 bytes...");
            for (i = 0; i < 512; i = i + 1) begin
                spi_miso = i[0];  // Simulate data coming back

                mmio_write_reg(ADDR_SPI_DATA, i[7:0]);
                wait_spi_done();

                // Check progress every 64 bytes
                if ((i % 64) == 63) begin
                    mmio_read_reg(ADDR_SPI_STATUS, status);
                    $display("    Byte %0d/512: burst_count=%0d, burst_mode=%b",
                             i+1, status[15:3], status[2]);

                    if (i < 511 && status[2] != 1'b1) begin
                        $display("    ✗ Burst mode cleared prematurely at byte %0d!", i+1);
                        errors = errors + 1;
                    end
                end
            end

            // Verify burst complete
            $display("  4. Verifying burst completion");
            mmio_read_reg(ADDR_SPI_STATUS, status);
            $display("    Final status: 0x%08x", status);
            $display("      - burst_mode: %b", status[2]);
            $display("      - burst_count: %d", status[15:3]);

            if (status[2] == 1'b0 && status[15:3] == 0) begin
                $display("  ✓ Burst transfer completed correctly");
                if (errors == 0) begin
                    test_passed = test_passed + 1;
                end else begin
                    $display("  ✗ Burst completed but had %0d errors during transfer", errors);
                    test_failed = test_failed + 1;
                end
            end else begin
                $display("  ✗ Burst transfer did not complete correctly");
                test_failed = test_failed + 1;
            end
        end

        //======================================================================
        // Summary
        //======================================================================
        $display("\n========================================");
        $display("Test Suite Complete");
        $display("========================================");
        $display("Total Tests:  %0d", test_count);
        $display("Passed:       %0d", test_passed);
        $display("Failed:       %0d", test_failed);
        $display("");

        if (test_failed == 0) begin
            $display("✓ ALL TESTS PASSED - MMIO integration verified");
        end else begin
            $display("✗ %0d TESTS FAILED - MMIO integration broken", test_failed);
        end
        $display("========================================\n");

        $finish;
    end

    // Timeout watchdog
    initial begin
        #100_000_000;  // 100ms timeout
        $display("\n[ERROR] Simulation timeout!");
        $finish;
    end

endmodule
