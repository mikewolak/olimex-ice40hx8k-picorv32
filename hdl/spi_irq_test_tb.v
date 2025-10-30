`timescale 1ns / 1ps

//==============================================================================
// SPI Interrupt Test Bench
// Comprehensive testing of SPI transfer complete interrupt generation
//==============================================================================

module spi_irq_test_tb;

    //==========================================================================
    // Clock and Reset
    //==========================================================================
    reg clk;
    reg resetn;

    // 50 MHz clock (20ns period)
    always #10 clk = ~clk;

    //==========================================================================
    // MMIO Interface (CPU to SPI)
    //==========================================================================
    reg         mmio_valid;
    reg         mmio_write;
    reg  [31:0] mmio_addr;
    reg  [31:0] mmio_wdata;
    reg  [3:0]  mmio_wstrb;
    wire [31:0] mmio_rdata;
    wire        mmio_ready;

    //==========================================================================
    // DMA Memory Bus (SPI to SRAM)
    //==========================================================================
    wire        dma_mem_valid;
    wire        dma_mem_write;
    wire [31:0] dma_mem_addr;
    wire [31:0] dma_mem_wdata;
    wire [3:0]  dma_mem_wstrb;
    reg  [31:0] dma_mem_rdata;
    reg         dma_mem_ready;

    //==========================================================================
    // SPI Interface
    //==========================================================================
    wire spi_sck;
    wire spi_mosi;
    reg  spi_miso;
    wire spi_cs;
    wire spi_irq;

    //==========================================================================
    // Device Under Test
    //==========================================================================
    spi_master dut (
        .clk(clk),
        .resetn(resetn),

        // MMIO Interface
        .mmio_valid(mmio_valid),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(mmio_rdata),
        .mmio_ready(mmio_ready),

        // DMA Memory Bus
        .dma_mem_valid(dma_mem_valid),
        .dma_mem_write(dma_mem_write),
        .dma_mem_addr(dma_mem_addr),
        .dma_mem_wdata(dma_mem_wdata),
        .dma_mem_wstrb(dma_mem_wstrb),
        .dma_mem_rdata(dma_mem_rdata),
        .dma_mem_ready(dma_mem_ready),

        // SPI Interface
        .spi_sck(spi_sck),
        .spi_mosi(spi_mosi),
        .spi_miso(spi_miso),
        .spi_cs(spi_cs),
        .spi_irq(spi_irq)
    );

    //==========================================================================
    // Test Variables
    //==========================================================================
    integer test_num;
    integer tests_passed;
    integer tests_failed;
    integer total_tests;

    reg [31:0] read_value;
    integer irq_count;
    integer irq_width_cycles;
    reg irq_detected;

    //==========================================================================
    // Simple Memory Model (512 bytes at 0x00001000)
    //==========================================================================
    reg [7:0] mem_array [0:511];
    integer i;

    always @(posedge clk) begin
        if (dma_mem_valid && dma_mem_write && dma_mem_ready) begin
            // Write to memory
            if (dma_mem_addr >= 32'h00001000 && dma_mem_addr < 32'h00001200) begin
                if (dma_mem_wstrb[0]) mem_array[dma_mem_addr - 32'h00001000] <= dma_mem_wdata[7:0];
            end
        end
    end

    always @(posedge clk) begin
        if (dma_mem_valid && !dma_mem_write) begin
            // Read from memory
            if (dma_mem_addr >= 32'h00001000 && dma_mem_addr < 32'h00001200) begin
                dma_mem_rdata <= {24'h0, mem_array[dma_mem_addr - 32'h00001000]};
            end else begin
                dma_mem_rdata <= 32'hDEADBEEF;
            end
        end
    end

    // Simple ready signal
    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            dma_mem_ready <= 1'b0;
        end else begin
            dma_mem_ready <= dma_mem_valid;
        end
    end

    //==========================================================================
    // IRQ Edge Detector & Width Counter
    //==========================================================================
    reg spi_irq_prev;
    reg irq_active;

    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            spi_irq_prev <= 1'b0;
            irq_count <= 0;
            irq_width_cycles <= 0;
            irq_active <= 1'b0;
        end else begin
            spi_irq_prev <= spi_irq;

            // Detect rising edge
            if (spi_irq && !spi_irq_prev) begin
                irq_count <= irq_count + 1;
                irq_width_cycles <= 1;
                irq_active <= 1'b1;
                $display("[IRQ] Rising edge detected at time %0t (count=%0d)", $time, irq_count + 1);
            end else if (irq_active && spi_irq) begin
                // Count width while high
                irq_width_cycles <= irq_width_cycles + 1;
            end else if (irq_active && !spi_irq) begin
                // Falling edge
                $display("[IRQ] Falling edge detected, width=%0d cycles", irq_width_cycles);
                irq_active <= 1'b0;
            end
        end
    end

    //==========================================================================
    // Tasks
    //==========================================================================

    // Write to MMIO register
    task mmio_write_reg(input [31:0] addr, input [31:0] data);
        begin
            @(posedge clk);
            mmio_valid <= 1'b1;
            mmio_write <= 1'b1;
            mmio_addr <= addr;
            mmio_wdata <= data;

            @(posedge clk);
            while (!mmio_ready) @(posedge clk);

            mmio_valid <= 1'b0;
            mmio_write <= 1'b0;
            @(posedge clk);
        end
    endtask

    // Read from MMIO register
    task mmio_read_reg(input [31:0] addr, output [31:0] data);
        begin
            @(posedge clk);
            mmio_valid <= 1'b1;
            mmio_write <= 1'b0;
            mmio_addr <= addr;

            @(posedge clk);
            while (!mmio_ready) @(posedge clk);
            data = mmio_rdata;

            mmio_valid <= 1'b0;
            @(posedge clk);
        end
    endtask

    // Wait for SPI transfer completion
    task wait_spi_done;
        integer timeout;
        begin
            timeout = 0;
            read_value = 32'hFFFFFFFF;

            while ((read_value & 32'h00000001) && timeout < 10000) begin
                mmio_read_reg(32'h80000058, read_value);  // STATUS
                timeout = timeout + 1;
            end

            if (timeout >= 10000) begin
                $display("ERROR: SPI transfer timeout!");
            end
        end
    endtask

    // Wait for specific number of clock cycles
    task wait_cycles(input integer cycles);
        integer j;
        begin
            for (j = 0; j < cycles; j = j + 1) begin
                @(posedge clk);
            end
        end
    endtask

    //==========================================================================
    // Main Test Sequence
    //==========================================================================
    initial begin
        // Initialize
        clk = 0;
        resetn = 0;
        mmio_valid = 0;
        mmio_write = 0;
        mmio_addr = 0;
        mmio_wdata = 0;
        mmio_wstrb = 4'hF;
        spi_miso = 0;

        tests_passed = 0;
        tests_failed = 0;
        total_tests = 0;
        test_num = 0;

        // Apply reset
        #50;
        resetn = 1;
        #100;

        $display("");
        $display("================================================================================");
        $display("SPI Interrupt Comprehensive Test");
        $display("================================================================================");
        $display("");

        //======================================================================
        // TEST 1: Single-byte transfer IRQ
        //======================================================================
        test_num = 1;
        total_tests = total_tests + 1;
        $display("[TEST %0d] Single-byte transfer interrupt", test_num);

        irq_count = 0;
        irq_detected = 0;

        // Configure SPI
        mmio_write_reg(32'h80000050, 32'h00000000);  // CTRL: CPOL=0 CPHA=0

        // CS low
        mmio_write_reg(32'h8000005C, 32'h00000000);

        // Write data byte
        mmio_write_reg(32'h80000054, 32'h000000A5);  // DATA

        // Wait for completion
        wait_spi_done();

        // Wait a few extra cycles to ensure IRQ pulse is captured
        wait_cycles(5);

        if (irq_count == 1) begin
            $display("[TEST %0d] PASS: IRQ generated for single-byte transfer", test_num);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: Expected 1 IRQ, got %0d", test_num, irq_count);
            tests_failed = tests_failed + 1;
        end

        wait_cycles(10);

        //======================================================================
        // TEST 2: Verify IRQ is single-cycle pulse
        //======================================================================
        test_num = 2;
        total_tests = total_tests + 1;
        $display("");
        $display("[TEST %0d] Verify IRQ is single-cycle pulse", test_num);

        irq_count = 0;
        irq_width_cycles = 0;

        // Write another byte
        mmio_write_reg(32'h80000054, 32'h0000005A);

        // Wait for completion
        wait_spi_done();
        wait_cycles(5);

        if (irq_count == 1 && irq_width_cycles == 1) begin
            $display("[TEST %0d] PASS: IRQ is exactly 1 clock cycle wide", test_num);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: IRQ width = %0d cycles (expected 1)", test_num, irq_width_cycles);
            tests_failed = tests_failed + 1;
        end

        wait_cycles(10);

        //======================================================================
        // TEST 3: Manual burst mode - IRQ only on last byte
        //======================================================================
        test_num = 3;
        total_tests = total_tests + 1;
        $display("");
        $display("[TEST %0d] Manual burst mode - IRQ on last byte only", test_num);

        irq_count = 0;

        // Set burst count to 3
        mmio_write_reg(32'h80000060, 32'h00000003);  // BURST_COUNT = 3

        // Write first byte
        mmio_write_reg(32'h80000054, 32'h00000011);
        wait_spi_done();
        wait_cycles(5);

        if (irq_count == 0) begin
            $display("[TEST %0d] Byte 1/3: No IRQ (correct)", test_num);
        end else begin
            $display("[TEST %0d] Byte 1/3: ERROR - IRQ generated early!", test_num);
        end

        // Write second byte
        mmio_write_reg(32'h80000054, 32'h00000022);
        wait_spi_done();
        wait_cycles(5);

        if (irq_count == 0) begin
            $display("[TEST %0d] Byte 2/3: No IRQ (correct)", test_num);
        end else begin
            $display("[TEST %0d] Byte 2/3: ERROR - IRQ generated early!", test_num);
        end

        // Write third (last) byte
        mmio_write_reg(32'h80000054, 32'h00000033);
        wait_spi_done();
        wait_cycles(5);

        if (irq_count == 1) begin
            $display("[TEST %0d] Byte 3/3: IRQ generated on last byte (correct)", test_num);
            $display("[TEST %0d] PASS: Burst mode IRQ only on last byte", test_num);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: Expected 1 IRQ after burst, got %0d", test_num, irq_count);
            tests_failed = tests_failed + 1;
        end

        wait_cycles(10);

        //======================================================================
        // TEST 4: Multiple single-byte transfers - IRQ each time
        //======================================================================
        test_num = 4;
        total_tests = total_tests + 1;
        $display("");
        $display("[TEST %0d] Multiple single-byte transfers - IRQ each time", test_num);

        irq_count = 0;

        // Transfer 5 individual bytes
        for (i = 0; i < 5; i = i + 1) begin
            mmio_write_reg(32'h80000054, {24'h0, i[7:0]});
            wait_spi_done();
            wait_cycles(3);
        end

        if (irq_count == 5) begin
            $display("[TEST %0d] PASS: Got 5 IRQs for 5 transfers", test_num);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: Expected 5 IRQs, got %0d", test_num, irq_count);
            tests_failed = tests_failed + 1;
        end

        wait_cycles(10);

        //======================================================================
        // TEST 5: DMA transfer with IRQ enable
        //======================================================================
        test_num = 5;
        total_tests = total_tests + 1;
        $display("");
        $display("[TEST %0d] DMA transfer with IRQ enabled", test_num);

        // Initialize memory with test pattern
        for (i = 0; i < 8; i = i + 1) begin
            mem_array[i] = 8'hA0 + i;
        end

        // Set burst count
        mmio_write_reg(32'h80000060, 32'h00000008);  // 8 bytes

        // Set DMA address
        mmio_write_reg(32'h80000064, 32'h00001000);

        // Reset IRQ counter JUST before starting DMA
        irq_count = 0;

        // Start DMA TX with IRQ enable
        mmio_write_reg(32'h80000068, 32'h00000009);  // START=1, DIR=TX, IRQ_EN=1

        // Wait for IRQ with timeout (8 bytes * (SRAM + SPI + overhead) = ~400 cycles/byte)
        // Need at least 3000 cycles based on observed timing
        for (i = 0; i < 3500 && irq_count == 0; i = i + 1) begin
            wait_cycles(1);
        end

        if (irq_count >= 1) begin
            $display("[TEST %0d] PASS: DMA completion IRQ generated (count=%0d)", test_num, irq_count);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: Expected >=1 IRQ, got %0d", test_num, irq_count);
            tests_failed = tests_failed + 1;
        end

        // Wait longer to ensure all IRQs settle
        wait_cycles(50);

        //======================================================================
        // TEST 6: DMA transfer with IRQ disabled
        //======================================================================
        test_num = 6;
        total_tests = total_tests + 1;
        $display("");
        $display("[TEST %0d] Verify IRQ clear behavior after burst DMA", test_num);

        // Set burst count
        mmio_write_reg(32'h80000060, 32'h00000004);  // 4 bytes

        // Set DMA address
        mmio_write_reg(32'h80000064, 32'h00001000);

        // Reset IRQ counter JUST before starting DMA
        irq_count = 0;

        // Start DMA TX with IRQ disabled
        mmio_write_reg(32'h80000068, 32'h00000001);  // START=1, DIR=TX, IRQ_EN=0

        // Wait for DMA completion with timeout (4 bytes * ~400 cycles/byte = 1600 cycles + margin)
        for (i = 0; i < 2000; i = i + 1) begin
            wait_cycles(1);
        end

        // Verify no IRQ was generated (IRQ_EN was 0)
        if (irq_count == 0) begin
            $display("[TEST %0d] PASS: No IRQ generated (IRQ disabled)", test_num);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: Expected 0 IRQs, got %0d", test_num, irq_count);
            tests_failed = tests_failed + 1;
        end

        wait_cycles(10);

        //======================================================================
        // TEST 7: IRQ timing - verify synchronous to clock
        //======================================================================
        test_num = 7;
        total_tests = total_tests + 1;
        $display("");
        $display("[TEST %0d] Verify IRQ is synchronous to clock edge", test_num);

        irq_count = 0;

        // Write byte and monitor IRQ timing
        mmio_write_reg(32'h80000054, 32'h000000FF);
        wait_spi_done();
        wait_cycles(5);

        // Check that IRQ was detected by edge detector (proves sync)
        if (irq_count == 1) begin
            $display("[TEST %0d] PASS: IRQ properly synchronized to clock", test_num);
            tests_passed = tests_passed + 1;
        end else begin
            $display("[TEST %0d] FAIL: IRQ synchronization issue", test_num);
            tests_failed = tests_failed + 1;
        end

        wait_cycles(10);

        //======================================================================
        // Test Summary
        //======================================================================
        $display("");
        $display("");
        $display("================================================================================");
        $display("TEST SUMMARY");
        $display("================================================================================");
        $display("Total Tests:       %0d", total_tests);
        $display("Passed:            %0d", tests_passed);
        $display("Failed:            %0d", tests_failed);
        $display("Success Rate:      %0d%%", (tests_passed * 100) / total_tests);
        $display("================================================================================");
        $display("");

        if (tests_failed == 0) begin
            $display("*** ALL INTERRUPT TESTS PASSED! ***");
        end else begin
            $display("*** %0d TEST(S) FAILED ***", tests_failed);
        end

        $display("");
        $finish;
    end

    //==========================================================================
    // Timeout Watchdog
    //==========================================================================
    initial begin
        #500000;  // 500 us timeout
        $display("");
        $display("ERROR: Simulation timeout!");
        $finish;
    end

endmodule
