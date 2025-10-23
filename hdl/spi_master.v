//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// spi_master.v - Minimal Gate-Efficient SPI Master Peripheral
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

module spi_master (
    input wire clk,           // 50 MHz system clock
    input wire resetn,        // Active-low reset

    // MMIO Interface (from mmio_peripherals)
    input wire        mmio_valid,
    input wire        mmio_write,
    input wire [31:0] mmio_addr,
    input wire [31:0] mmio_wdata,
    input wire [ 3:0] mmio_wstrb,
    output reg [31:0] mmio_rdata,
    output reg        mmio_ready,

    // SPI Physical Interface
    output reg        spi_sck,     // SPI clock
    output reg        spi_mosi,    // Master Out Slave In
    input wire        spi_miso,    // Master In Slave Out
    output reg        spi_cs       // Chip Select (active low)
);

    //==========================================================================
    // Memory Map (Base: 0x80000050)
    //==========================================================================
    localparam ADDR_SPI_CTRL   = 32'h80000050;  // Control register
    localparam ADDR_SPI_DATA   = 32'h80000054;  // Data register
    localparam ADDR_SPI_STATUS = 32'h80000058;  // Status register
    localparam ADDR_SPI_CS     = 32'h8000005C;  // Chip select control

    //==========================================================================
    // Configuration Registers
    //==========================================================================
    reg        cpol;          // Clock polarity (0=idle low, 1=idle high)
    reg        cpha;          // Clock phase (0=sample on leading, 1=trailing)
    reg [2:0]  clk_div;       // Clock divider: 000=/1, 001=/2, 010=/4, 011=/8, 100=/16, 101=/32, 110=/64, 111=/128
    reg        cs_manual;     // Manual chip select control

    //==========================================================================
    // Data Registers
    //==========================================================================
    reg [7:0]  tx_data;       // Transmit data register
    reg [7:0]  rx_data;       // Receive data register
    reg        tx_valid;      // Transmit request flag
    reg        busy;          // Transfer in progress
    reg        done;          // Transfer complete flag

    //==========================================================================
    // SPI Core State Machine
    //==========================================================================
    localparam STATE_IDLE     = 2'b00;
    localparam STATE_TRANSMIT = 2'b01;
    localparam STATE_FINISH   = 2'b10;

    reg [1:0]  state;
    reg [2:0]  bit_count;     // 0-7 for 8 bits
    reg [7:0]  shift_reg;     // Shift register for TX/RX

    //==========================================================================
    // Clock Divider Logic - Gate-Efficient Power-of-2 Divider
    // Supports /1, /2, /4, /8, /16, /32, /64, /128 for SD card compatibility
    //==========================================================================
    reg [6:0]  clk_counter;   // 0-127 counter for maximum /128 division
    reg        spi_clk_en;    // Clock enable pulse

    // Divider thresholds (all power-of-2 for simple bit checking)
    wire [6:0] div_threshold;
    assign div_threshold = (7'b1 << clk_div) - 1'b1;  // 2^clk_div - 1

    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            clk_counter <= 7'b0;
            spi_clk_en <= 1'b0;
        end else begin
            // Generate enable pulse when counter reaches threshold
            if (clk_counter == div_threshold) begin
                spi_clk_en <= 1'b1;
                clk_counter <= 7'b0;
            end else begin
                spi_clk_en <= 1'b0;
                clk_counter <= clk_counter + 1'b1;
            end
        end
    end

    //==========================================================================
    // SPI State Machine
    //==========================================================================
    reg sck_phase;  // Internal clock phase tracker

    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            state <= STATE_IDLE;
            spi_sck <= 1'b0;
            spi_mosi <= 1'b0;
            shift_reg <= 8'h00;
            rx_data <= 8'h00;
            bit_count <= 3'b000;
            busy <= 1'b0;
            done <= 1'b0;
            sck_phase <= 1'b0;
        end else begin
            case (state)
                STATE_IDLE: begin
                    busy <= 1'b0;
                    spi_sck <= cpol;  // Idle state based on polarity
                    sck_phase <= 1'b0;

                    if (tx_valid) begin
                        // Load shift register and start transfer
                        shift_reg <= tx_data;
                        bit_count <= 3'b000;
                        busy <= 1'b1;
                        done <= 1'b0;
                        state <= STATE_TRANSMIT;

                        // Set MOSI for first bit if CPHA=0
                        if (!cpha) begin
                            spi_mosi <= tx_data[7];
                        end
                    end
                end

                STATE_TRANSMIT: begin
                    if (spi_clk_en) begin
                        sck_phase <= ~sck_phase;

                        // Toggle SPI clock
                        spi_sck <= sck_phase ? cpol : ~cpol;

                        if (!sck_phase) begin
                            // First edge (setup)
                            if (!cpha) begin
                                // CPHA=0: Sample on first edge, setup on second
                                shift_reg <= {shift_reg[6:0], spi_miso};
                            end else begin
                                // CPHA=1: Setup on first edge
                                spi_mosi <= shift_reg[7];
                            end
                        end else begin
                            // Second edge (sample/shift)
                            if (!cpha) begin
                                // CPHA=0: Setup next bit
                                bit_count <= bit_count + 1'b1;
                                if (bit_count < 7) begin
                                    spi_mosi <= shift_reg[6];
                                end
                            end else begin
                                // CPHA=1: Sample on second edge
                                shift_reg <= {shift_reg[6:0], spi_miso};
                                bit_count <= bit_count + 1'b1;
                            end

                            // Check if transfer complete (after 8 bits)
                            if (bit_count == 3'b111) begin
                                state <= STATE_FINISH;
                            end
                        end
                    end
                end

                STATE_FINISH: begin
                    // Return clock to idle state and save received data
                    spi_sck <= cpol;
                    rx_data <= shift_reg;
                    done <= 1'b1;
                    busy <= 1'b0;
                    state <= STATE_IDLE;
                end

                default: state <= STATE_IDLE;
            endcase
        end
    end

    //==========================================================================
    // MMIO Register Interface
    //==========================================================================
    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            mmio_rdata <= 32'h0;
            mmio_ready <= 1'b0;
            cpol <= 1'b0;
            cpha <= 1'b0;
            clk_div <= 3'b111;    // Default: /128 = 390 kHz (SD card init safe)
            cs_manual <= 1'b1;    // Default: CS high (inactive)
            spi_cs <= 1'b1;
            tx_data <= 8'h00;
            tx_valid <= 1'b0;
        end else begin
            // Clear control signals
            mmio_ready <= 1'b0;
            tx_valid <= 1'b0;

            // Update CS from manual control
            spi_cs <= cs_manual;

            if (mmio_valid && !mmio_ready) begin
                if (mmio_write) begin
                    // ============ WRITE OPERATIONS ============
                    case (mmio_addr)
                        ADDR_SPI_CTRL: begin
                            // Write to control register
                            if (mmio_wstrb[0]) begin
                                cpol <= mmio_wdata[0];
                                cpha <= mmio_wdata[1];
                                clk_div <= mmio_wdata[4:2];
                            end
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[SPI] CTRL: CPOL=%b CPHA=%b CLK_DIV=%b",
                                     mmio_wdata[0], mmio_wdata[1], mmio_wdata[4:2]);
                            // synthesis translate_on
                        end

                        ADDR_SPI_DATA: begin
                            // Write to data register (initiate transfer)
                            if (!busy && mmio_wstrb[0]) begin
                                tx_data <= mmio_wdata[7:0];
                                tx_valid <= 1'b1;
                                mmio_ready <= 1'b1;

                                // synthesis translate_off
                                $display("[SPI] TX: 0x%02x", mmio_wdata[7:0]);
                                // synthesis translate_on
                            end
                            // If busy, don't ack - CPU must retry
                        end

                        ADDR_SPI_CS: begin
                            // Write to chip select register
                            if (mmio_wstrb[0]) begin
                                cs_manual <= mmio_wdata[0];
                            end
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[SPI] CS: %b", mmio_wdata[0]);
                            // synthesis translate_on
                        end

                        default: begin
                            mmio_ready <= 1'b1;
                        end
                    endcase

                end else begin
                    // ============ READ OPERATIONS ============
                    case (mmio_addr)
                        ADDR_SPI_CTRL: begin
                            // Read control register
                            mmio_rdata <= {27'h0, clk_div, cpha, cpol};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_DATA: begin
                            // Read received data
                            mmio_rdata <= {24'h0, rx_data};
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[SPI] RX: 0x%02x", rx_data);
                            // synthesis translate_on
                        end

                        ADDR_SPI_STATUS: begin
                            // Read status register
                            mmio_rdata <= {30'h0, done, busy};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_CS: begin
                            // Read chip select state
                            mmio_rdata <= {31'h0, cs_manual};
                            mmio_ready <= 1'b1;
                        end

                        default: begin
                            mmio_rdata <= 32'h0;
                            mmio_ready <= 1'b1;
                        end
                    endcase
                end
            end
        end
    end

endmodule
