//==============================================================================
// Full System Testbench - PicoRV32 + Memory Controller + SRAM
//
// Tests the complete system by loading firmware and running it
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

`timescale 1ns/1ns

module tb_full_system;

    // Clock generation - 100MHz external clock
    reg EXTCLK = 0;
    always #5 EXTCLK = ~EXTCLK;  // 100 MHz = 10ns period

    // Test signals
    reg BUT1 = 1'b1;  // Active low, so 1 = not pressed
    reg BUT2 = 1'b1;
    wire LED1, LED2;

    // UART signals
    wire UART_TX, UART_RX;
    assign UART_RX = 1'b1;  // Idle state

    // SPI signals (unused in this test)
    wire SPI_SCK, SPI_MOSI, SPI_CS;
    wire SPI_MISO = 1'b0;

    // SRAM signals
    wire [17:0] SA;
    wire [15:0] SD;
    wire SRAM_CS_N, SRAM_OE_N, SRAM_WE_N;

    // Instantiate the full system
    ice40_picorv32_top uut (
        .EXTCLK(EXTCLK),
        .BUT1(BUT1),
        .BUT2(BUT2),
        .LED1(LED1),
        .LED2(LED2),
        .UART_RX(UART_RX),
        .UART_TX(UART_TX),
        .SPI_SCK(SPI_SCK),
        .SPI_MOSI(SPI_MOSI),
        .SPI_MISO(SPI_MISO),
        .SPI_CS(SPI_CS),
        .SA(SA),
        .SD(SD),
        .SRAM_CS_N(SRAM_CS_N),
        .SRAM_OE_N(SRAM_OE_N),
        .SRAM_WE_N(SRAM_WE_N)
    );

    // Instantiate SRAM model
    sram_model sram (
        .addr(SA),
        .data(SD),
        .cs_n(SRAM_CS_N),
        .oe_n(SRAM_OE_N),
        .we_n(SRAM_WE_N)
    );

    // UART monitor - capture and display UART output
    reg [7:0] uart_bit_counter = 0;
    reg [9:0] uart_shift_reg = 0;
    reg uart_tx_prev = 1;

    always @(posedge EXTCLK) begin
        uart_tx_prev <= UART_TX;

        // Detect start bit (falling edge)
        if (uart_tx_prev && !UART_TX && uart_bit_counter == 0) begin
            uart_bit_counter <= 1;
            uart_shift_reg <= 0;
        end
        else if (uart_bit_counter > 0) begin
            // Sample at middle of bit period (assuming 115200 baud)
            // At 100MHz, 115200 baud = 868 clocks per bit
            // Sample at clock 434 (middle)
            if (uart_bit_counter == 434) begin
                uart_shift_reg <= {UART_TX, uart_shift_reg[9:1]};
            end

            uart_bit_counter <= uart_bit_counter + 1;

            // After 868 clocks, we've received one bit
            if (uart_bit_counter == 868) begin
                uart_bit_counter <= 0;

                // Check if we have a complete byte (start + 8 data + stop)
                if (uart_shift_reg[9] == 1 && uart_shift_reg[0] == 0) begin
                    $write("%c", uart_shift_reg[8:1]);
                    $fflush;
                end
            end
        end
    end

    // Monitor LED changes
    reg [1:0] led_prev;
    initial led_prev = 2'b00;

    always @(posedge EXTCLK) begin
        if ({LED2, LED1} != led_prev) begin
            $display("[LED] @ %0t: LED1=%b LED2=%b", $time, LED1, LED2);
            led_prev <= {LED2, LED1};
        end
    end

    // Test control
    initial begin
        $dumpfile("tb_full_system.vcd");
        $dumpvars(0, tb_full_system);

        $display("========================================");
        $display("Full System Test - PicoRV32 + SRAM");
        $display("LED Blink Firmware Test");
        $display("========================================");
        $display("");
        $display("Waiting for firmware execution...");
        $display("");

        // Wait for LED blinking to occur (need longer time for delay loops)
        #50000000;  // 50ms - enough for several LED transitions

        $display("");
        $display("========================================");
        $display("Test Complete");
        $display("========================================");

        $finish;
    end

    // Timeout watchdog
    initial begin
        #100000000;  // 100ms timeout
        $display("");
        $display("ERROR: Simulation timeout!");
        $finish;
    end

endmodule
