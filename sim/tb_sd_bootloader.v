//==============================================================================
// SD Bootloader Testbench - Full Boot Sequence Verification
//
// Tests:
// 1. Bootloader ROM execution from 0x40000
// 2. UART output (banner, status messages)
// 3. SD card SPI initialization
// 4. SD card sector read operations
// 5. Data loading to RAM at 0x0
// 6. Jump to loaded firmware
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

`timescale 1ns/1ns

module tb_sd_bootloader;

    // Clock generation - 100MHz external clock (50 MHz after divide-by-2)
    reg EXTCLK = 0;
    always #5 EXTCLK = ~EXTCLK;  // 100 MHz = 10ns period

    // Test signals
    reg BUT1 = 1'b1;  // Active low
    reg BUT2 = 1'b1;
    wire LED1, LED2;

    // UART signals
    wire UART_TX, UART_RX;
    assign UART_RX = 1'b1;  // Idle state

    // SPI signals for SD card
    wire SPI_SCK, SPI_MOSI, SPI_CS;
    reg SPI_MISO;

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

    //==========================================================================
    // SD Card SPI Model
    //==========================================================================

    // SD card memory (first 376 sectors = 192KB)
    reg [7:0] sd_card_memory [0:(376*512)-1];

    // SPI state machine
    reg [7:0] spi_rx_byte;
    reg [7:0] spi_tx_byte;
    reg [2:0] spi_bit_count;
    reg [7:0] spi_cmd_buffer [0:5];  // CMD + 4 byte arg + CRC
    reg [2:0] spi_cmd_byte_count;
    reg [3:0] sd_state;
    reg [31:0] sd_read_addr;
    reg [15:0] sd_byte_counter;

    localparam SD_IDLE = 0;
    localparam SD_CMD_RX = 1;
    localparam SD_CMD_PROCESS = 2;
    localparam SD_R1_RESPONSE = 3;
    localparam SD_R3_RESPONSE = 4;
    localparam SD_R7_RESPONSE = 5;
    localparam SD_READ_START = 6;
    localparam SD_READ_DATA = 7;
    localparam SD_READ_CRC = 8;

    initial begin
        // Initialize SD card with test pattern
        integer i;
        for (i = 0; i < (376*512); i = i + 1) begin
            // Fill with pattern: byte address mod 256
            sd_card_memory[i] = i[7:0];
        end

        // Put a recognizable signature at start of sector 1
        sd_card_memory[512] = 8'hDE;
        sd_card_memory[513] = 8'hAD;
        sd_card_memory[514] = 8'hBE;
        sd_card_memory[515] = 8'hEF;

        SPI_MISO = 1'b1;
        spi_bit_count = 0;
        spi_cmd_byte_count = 0;
        sd_state = SD_IDLE;
    end

    // SPI shift register on SCK
    always @(posedge SPI_SCK or posedge SPI_CS) begin
        if (SPI_CS) begin
            // CS deasserted - reset
            spi_bit_count <= 0;
            spi_cmd_byte_count <= 0;
        end else begin
            // Shift in MOSI bit
            spi_rx_byte <= {spi_rx_byte[6:0], SPI_MOSI};
            spi_bit_count <= spi_bit_count + 1;

            if (spi_bit_count == 7) begin
                // Byte received
                if (sd_state == SD_IDLE || sd_state == SD_CMD_RX) begin
                    spi_cmd_buffer[spi_cmd_byte_count] <= {spi_rx_byte[6:0], SPI_MOSI};
                    spi_cmd_byte_count <= spi_cmd_byte_count + 1;

                    if (spi_cmd_byte_count == 5) begin
                        // Full command received
                        sd_state <= SD_CMD_PROCESS;
                    end else begin
                        sd_state <= SD_CMD_RX;
                    end
                end
            end
        end
    end

    // SD card command processing
    always @(posedge EXTCLK) begin
        case (sd_state)
            SD_CMD_PROCESS: begin
                // Process received command
                case (spi_cmd_buffer[0][5:0])  // CMD index
                    0: begin  // CMD0 - GO_IDLE_STATE
                        spi_tx_byte <= 8'h01;  // R1: In idle state
                        sd_state <= SD_R1_RESPONSE;
                        $display("[SD_CARD] @ %0t: CMD0 GO_IDLE_STATE", $time);
                    end

                    8: begin  // CMD8 - SEND_IF_COND
                        spi_tx_byte <= 8'h01;  // R1: In idle state
                        sd_state <= SD_R7_RESPONSE;
                        $display("[SD_CARD] @ %0t: CMD8 SEND_IF_COND", $time);
                    end

                    58: begin  // CMD58 - READ_OCR
                        spi_tx_byte <= 8'h00;  // R1: Ready
                        sd_state <= SD_R3_RESPONSE;
                        $display("[SD_CARD] @ %0t: CMD58 READ_OCR", $time);
                    end

                    55: begin  // CMD55 - APP_CMD
                        spi_tx_byte <= 8'h00;  // R1: Ready
                        sd_state <= SD_R1_RESPONSE;
                        $display("[SD_CARD] @ %0t: CMD55 APP_CMD", $time);
                    end

                    41: begin  // ACMD41 - SD_SEND_OP_COND
                        spi_tx_byte <= 8'h00;  // R1: Initialization complete
                        sd_state <= SD_R1_RESPONSE;
                        $display("[SD_CARD] @ %0t: ACMD41 SD_SEND_OP_COND", $time);
                    end

                    17: begin  // CMD17 - READ_SINGLE_BLOCK
                        sd_read_addr <= {spi_cmd_buffer[1], spi_cmd_buffer[2], spi_cmd_buffer[3], spi_cmd_buffer[4]};
                        spi_tx_byte <= 8'h00;  // R1: OK
                        sd_state <= SD_READ_START;
                        $display("[SD_CARD] @ %0t: CMD17 READ_SINGLE_BLOCK addr=0x%08h", $time, {spi_cmd_buffer[1], spi_cmd_buffer[2], spi_cmd_buffer[3], spi_cmd_buffer[4]});
                    end

                    default: begin
                        spi_tx_byte <= 8'hFF;  // Invalid command
                        sd_state <= SD_R1_RESPONSE;
                        $display("[SD_CARD] @ %0t: Unknown CMD%0d", $time, spi_cmd_buffer[0][5:0]);
                    end
                endcase

                spi_cmd_byte_count <= 0;
            end

            SD_READ_START: begin
                // Send data start token
                spi_tx_byte <= 8'hFE;
                sd_byte_counter <= 0;
                sd_state <= SD_READ_DATA;
            end

            SD_READ_DATA: begin
                // Send sector data
                spi_tx_byte <= sd_card_memory[sd_read_addr + sd_byte_counter];
                sd_byte_counter <= sd_byte_counter + 1;

                if (sd_byte_counter == 511) begin
                    sd_state <= SD_READ_CRC;
                end
            end

            SD_READ_CRC: begin
                // Send dummy CRC bytes
                spi_tx_byte <= 8'hFF;
                sd_state <= SD_IDLE;
            end
        endcase
    end

    // Shift out MISO bit
    always @(negedge SPI_SCK) begin
        if (!SPI_CS) begin
            SPI_MISO <= spi_tx_byte[7];
            spi_tx_byte <= {spi_tx_byte[6:0], 1'b1};
        end
    end

    //==========================================================================
    // UART Monitor (1 Mbaud at 50 MHz = 50 clocks per bit)
    //==========================================================================

    reg [7:0] uart_bit_counter = 0;
    reg [9:0] uart_shift_reg = 0;
    reg uart_tx_prev = 1;
    reg [7:0] uart_byte_count = 0;

    always @(posedge EXTCLK) begin
        uart_tx_prev <= UART_TX;

        // Detect start bit (falling edge)
        if (uart_tx_prev && !UART_TX && uart_bit_counter == 0) begin
            uart_bit_counter <= 1;
            uart_shift_reg <= 0;
        end
        else if (uart_bit_counter > 0) begin
            // At 100MHz with 1Mbaud: 100 clocks per bit
            // Sample at clock 50 (middle)
            if (uart_bit_counter == 50) begin
                uart_shift_reg <= {UART_TX, uart_shift_reg[9:1]};
            end

            uart_bit_counter <= uart_bit_counter + 1;

            // After 100 clocks, we've received one bit
            if (uart_bit_counter == 100) begin
                uart_bit_counter <= 0;

                // Check if we have a complete byte
                if (uart_shift_reg[9] == 1 && uart_shift_reg[0] == 0) begin
                    $write("%c", uart_shift_reg[8:1]);
                    $fflush;
                    uart_byte_count <= uart_byte_count + 1;
                end
            end
        end
    end

    //==========================================================================
    // Monitor LED changes
    //==========================================================================

    reg [1:0] led_prev;
    initial led_prev = 2'b00;

    always @(posedge EXTCLK) begin
        if ({LED2, LED1} != led_prev) begin
            $display("[LED] @ %0t: LED1=%b LED2=%b", $time, LED1, LED2);
            led_prev <= {LED2, LED1};
        end
    end

    //==========================================================================
    // Monitor bootloader ROM accesses
    //==========================================================================

    reg bootrom_access_detected = 0;

    always @(posedge EXTCLK) begin
        if (uut.cpu_mem_valid && uut.cpu_mem_addr >= 32'h00040000 && uut.cpu_mem_addr < 32'h00042000) begin
            if (!bootrom_access_detected) begin
                $display("[BOOTROM] @ %0t: First access to bootloader ROM at 0x%08h", $time, uut.cpu_mem_addr);
                bootrom_access_detected <= 1;
            end
        end
    end

    //==========================================================================
    // CPU Instruction Fetch Monitor
    //==========================================================================

    reg [31:0] last_fetch_addr = 0;
    reg [15:0] fetch_count = 0;
    reg [15:0] same_addr_count = 0;

    always @(posedge uut.clk) begin
        if (uut.cpu_mem_valid && uut.cpu_mem_instr && uut.cpu_mem_ready) begin
            fetch_count <= fetch_count + 1;

            // Track if CPU is stuck in a loop
            if (uut.cpu_mem_addr == last_fetch_addr) begin
                same_addr_count <= same_addr_count + 1;
                if (same_addr_count == 100) begin
                    $display("[CPU_STUCK] @ %0t: CPU fetching same address 100 times: 0x%08x", $time, uut.cpu_mem_addr);
                end
            end else begin
                same_addr_count <= 0;
            end

            last_fetch_addr <= uut.cpu_mem_addr;

            // Log first 50 instruction fetches
            if (fetch_count < 50) begin
                $display("[CPU_FETCH] @ %0t: PC=0x%08x data=0x%08x", $time, uut.cpu_mem_addr, uut.cpu_mem_rdata);
            end
        end
    end

    //==========================================================================
    // MMIO Access Monitor
    //==========================================================================

    always @(posedge uut.clk) begin
        if (uut.cpu_mem_valid && uut.cpu_mem_ready && !uut.cpu_mem_instr) begin
            // Check for UART writes (0x80000000)
            if (uut.cpu_mem_wstrb != 0 && uut.cpu_mem_addr == 32'h80000000) begin
                $display("[UART_WRITE] @ %0t: data=0x%02x ('%c')", $time, uut.cpu_mem_wdata[7:0], uut.cpu_mem_wdata[7:0]);
            end

            // Check for LED writes (0x80000010)
            if (uut.cpu_mem_wstrb != 0 && uut.cpu_mem_addr == 32'h80000010) begin
                $display("[LED_WRITE] @ %0t: data=0x%08x", $time, uut.cpu_mem_wdata);
            end

            // Check for SPI writes (0x80000030)
            if (uut.cpu_mem_wstrb != 0 && (uut.cpu_mem_addr & 32'hFFFFFFF0) == 32'h80000030) begin
                $display("[SPI_WRITE] @ %0t: addr=0x%08x data=0x%08x", $time, uut.cpu_mem_addr, uut.cpu_mem_wdata);
            end
        end
    end

    //==========================================================================
    // Test control
    //==========================================================================

    initial begin
        $dumpfile("tb_sd_bootloader.vcd");
        $dumpvars(0, tb_sd_bootloader);

        $display("========================================");
        $display("SD Bootloader Full Boot Test");
        $display("========================================");
        $display("");
        $display("Expected sequence:");
        $display("1. CPU boots from ROM at 0x40000");
        $display("2. UART banner output");
        $display("3. SD card initialization (CMD0, CMD8, CMD55, ACMD41, CMD58)");
        $display("4. SD card reads sectors 1-375");
        $display("5. Data loaded to RAM at 0x0");
        $display("6. Jump to 0x0");
        $display("");
        $display("Starting simulation...");
        $display("");

        // Run for sufficient time to complete boot sequence
        // SD init + 375 sector reads at ~50 clocks/byte = ~20M clocks = ~400ms
        #500000000;  // 500ms

        $display("");
        $display("========================================");
        $display("Test Complete");
        $display("UART bytes received: %0d", uart_byte_count);
        $display("========================================");

        $finish;
    end

    // Timeout watchdog
    initial begin
        #600000000;  // 600ms timeout
        $display("");
        $display("ERROR: Simulation timeout!");
        $finish;
    end

    // Success criteria checker
    initial begin
        #1000;  // Wait for startup

        // Wait for UART output
        wait (uart_byte_count > 10);
        $display("");
        $display("[PROGRESS] @ %0t: UART output detected (%0d bytes)", $time, uart_byte_count);
    end

endmodule
