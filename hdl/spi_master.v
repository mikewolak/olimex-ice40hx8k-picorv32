//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// spi_master.v - SPI Master with BSRAM TX FIFO Burst Transfer Support
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//
// ENHANCEMENT: Added 512-byte TX FIFO using iCE40 block RAM for high-
// performance burst WRITE transfers. RX uses direct register (no FIFO).
// Backward compatible with legacy single-byte mode.
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
    output reg        spi_cs,      // Chip Select (active low)

    // Interrupt
    output wire       spi_irq      // Transfer complete interrupt (single-cycle pulse)
);

    //==========================================================================
    // Memory Map (Base: 0x80000050)
    //==========================================================================
    localparam ADDR_SPI_CTRL        = 32'h80000050;  // Control register
    localparam ADDR_SPI_DATA        = 32'h80000054;  // Data register (TX FIFO port / RX direct)
    localparam ADDR_SPI_STATUS      = 32'h80000058;  // Status register (enhanced)
    localparam ADDR_SPI_CS          = 32'h8000005C;  // Chip select control
    localparam ADDR_SPI_XFER_COUNT  = 32'h80000060;  // Burst transfer count (NEW)
    localparam ADDR_SPI_FIFO_STATUS = 32'h80000064;  // TX FIFO level status (NEW)

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
    reg [7:0]  tx_data;       // Transmit data register (legacy single-byte)
    reg [7:0]  rx_data;       // Receive data register (always used - no RX FIFO)
    reg        tx_valid;      // Transmit request flag (legacy)
    reg        busy;          // Transfer in progress

    //==========================================================================
    // TX FIFO Control Registers
    //==========================================================================
    reg [8:0]  tx_wr_ptr;     // TX FIFO write pointer (CPU side)
    reg [8:0]  tx_rd_ptr;     // TX FIFO read pointer (SPI FSM side)
    reg [9:0]  tx_fifo_level; // TX FIFO occupancy (0-512)

    reg [9:0]  xfer_count;    // Burst transfer count (0=legacy mode, 1-512=burst)
    reg [9:0]  xfer_remaining;// Countdown during burst

    // TX FIFO status flags
    wire tx_fifo_full  = (tx_fifo_level == 10'd512);
    wire tx_fifo_empty = (tx_fifo_level == 10'd0);

    // Operating mode
    wire burst_mode = (xfer_count > 10'd0);

    // TX FIFO control signals
    reg        tx_fifo_wr_en;
    reg        tx_fifo_rd_en;

    // TX FIFO data bus
    wire [15:0] tx_fifo_rd_data;

    //==========================================================================
    // IRQ pulse generation
    //==========================================================================
    reg        irq_pulse;     // Single-cycle IRQ pulse
    assign spi_irq = irq_pulse;

    //==========================================================================
    // BSRAM TX FIFO (512 x 8 bits using SB_RAM40_4K)
    //==========================================================================
    SB_RAM40_4K #(
        .READ_MODE(1),    // 512x8 configuration
        .WRITE_MODE(1)    // 512x8 configuration
    ) tx_fifo (
        // Write port (CPU fills FIFO)
        .WDATA({8'h00, mmio_wdata[7:0]}),
        .WADDR({tx_wr_ptr}),
        .MASK(16'h0000),
        .WE(tx_fifo_wr_en),
        .WCLK(clk),
        .WCLKE(1'b1),

        // Read port (SPI FSM consumes FIFO)
        .RDATA(tx_fifo_rd_data),
        .RADDR({tx_rd_ptr}),
        .RE(1'b1),  // Always enabled, data ready 1 cycle after address
        .RCLK(clk),
        .RCLKE(1'b1)
    );

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
    // TX FIFO Pointer Management
    //==========================================================================
    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            tx_wr_ptr <= 9'b0;
            tx_rd_ptr <= 9'b0;
            tx_fifo_level <= 10'b0;
        end else begin
            // TX FIFO write (CPU)
            if (tx_fifo_wr_en && !tx_fifo_full) begin
                tx_wr_ptr <= tx_wr_ptr + 1'b1;
                tx_fifo_level <= tx_fifo_level + 1'b1;
            end

            // TX FIFO read (SPI FSM)
            if (tx_fifo_rd_en && !tx_fifo_empty) begin
                tx_rd_ptr <= tx_rd_ptr + 1'b1;
                tx_fifo_level <= tx_fifo_level - 1'b1;
            end

            // Handle simultaneous TX read/write
            if (tx_fifo_wr_en && tx_fifo_rd_en && !tx_fifo_full && !tx_fifo_empty) begin
                tx_fifo_level <= tx_fifo_level;  // No net change
            end
        end
    end

    //==========================================================================
    // SPI State Machine with TX FIFO Support
    //==========================================================================
    reg sck_phase;  // Internal clock phase tracker
    reg miso_captured;  // Captured MISO bit (for CPHA=0 mode)
    reg done;       // Transfer done flag

    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            state <= STATE_IDLE;
            spi_sck <= 1'b0;
            spi_mosi <= 1'b0;
            shift_reg <= 8'h00;
            rx_data <= 8'h00;
            bit_count <= 3'b000;
            busy <= 1'b0;
            sck_phase <= 1'b0;
            miso_captured <= 1'b0;
            irq_pulse <= 1'b0;
            done <= 1'b0;
            tx_fifo_rd_en <= 1'b0;
        end else begin
            // Default: Clear control signals
            irq_pulse <= 1'b0;
            tx_fifo_rd_en <= 1'b0;

            case (state)
                STATE_IDLE: begin
                    busy <= 1'b0;
                    spi_sck <= cpol;  // Idle state based on polarity
                    sck_phase <= 1'b0;

                    // Burst mode: start transfer from TX FIFO
                    if (burst_mode && !tx_fifo_empty && xfer_remaining > 10'd0) begin
                        tx_fifo_rd_en <= 1'b1;
                        shift_reg <= tx_fifo_rd_data[7:0];
                        bit_count <= 3'b000;
                        busy <= 1'b1;
                        done <= 1'b0;
                        xfer_remaining <= xfer_remaining - 1'b1;
                        state <= STATE_TRANSMIT;

                        // Set MOSI for first bit if CPHA=0
                        if (!cpha) begin
                            spi_mosi <= tx_fifo_rd_data[7];
                        end
                    end
                    // Legacy mode: single-byte direct transfer
                    else if (!burst_mode && tx_valid) begin
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
                            // First edge
                            if (!cpha) begin
                                // CPHA=0: Sample MISO on first edge (capture only, don't shift yet)
                                miso_captured <= spi_miso;
                            end else begin
                                // CPHA=1: Setup on first edge
                                spi_mosi <= shift_reg[7];
                            end
                        end else begin
                            // Second edge
                            if (!cpha) begin
                                // CPHA=0: Shift in captured bit and setup next MOSI
                                shift_reg <= {shift_reg[6:0], miso_captured};
                                bit_count <= bit_count + 1'b1;
                                if (bit_count < 7) begin
                                    // Output next bit (bit 6 of current shift_reg, before shift)
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
                    // Return clock to idle state
                    spi_sck <= cpol;

                    // Always store RX byte to rx_data register (no RX FIFO)
                    rx_data <= shift_reg;

                    // Check if more bytes to transfer in burst
                    if (burst_mode && xfer_remaining > 10'd0 && !tx_fifo_empty) begin
                        // Continue burst: fetch next byte from TX FIFO
                        tx_fifo_rd_en <= 1'b1;
                        shift_reg <= tx_fifo_rd_data[7:0];
                        bit_count <= 3'b000;
                        xfer_remaining <= xfer_remaining - 1'b1;
                        state <= STATE_TRANSMIT;

                        // Set MOSI for first bit if CPHA=0
                        if (!cpha) begin
                            spi_mosi <= tx_fifo_rd_data[7];
                        end
                    end else begin
                        // Burst complete or legacy transfer done
                        busy <= 1'b0;
                        done <= 1'b1;
                        irq_pulse <= 1'b1;  // Generate interrupt
                        state <= STATE_IDLE;
                    end
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
            tx_fifo_wr_en <= 1'b0;
            xfer_count <= 10'b0;
            xfer_remaining <= 10'b0;
        end else begin
            // Clear control signals
            mmio_ready <= 1'b0;
            tx_valid <= 1'b0;
            tx_fifo_wr_en <= 1'b0;

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
                            // Write to data register
                            if (mmio_wstrb[0]) begin
                                if (burst_mode) begin
                                    // Burst mode: write to TX FIFO
                                    if (!tx_fifo_full) begin
                                        tx_fifo_wr_en <= 1'b1;
                                        mmio_ready <= 1'b1;
                                    end
                                    // If FIFO full, don't ack - CPU must retry
                                end else begin
                                    // Legacy mode: direct byte transfer
                                    if (!busy) begin
                                        tx_data <= mmio_wdata[7:0];
                                        tx_valid <= 1'b1;
                                        mmio_ready <= 1'b1;

                                        // synthesis translate_off
                                        $display("[SPI] TX (legacy): 0x%02x", mmio_wdata[7:0]);
                                        // synthesis translate_on
                                    end
                                    // If busy, don't ack - CPU must retry
                                end
                            end
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

                        ADDR_SPI_XFER_COUNT: begin
                            // Write to transfer count (start burst)
                            if (mmio_wstrb[0] && !busy) begin
                                xfer_count <= mmio_wdata[9:0];
                                xfer_remaining <= mmio_wdata[9:0];
                                mmio_ready <= 1'b1;

                                // synthesis translate_off
                                $display("[SPI] XFER_COUNT: %d (burst mode %s)",
                                         mmio_wdata[9:0],
                                         (mmio_wdata[9:0] > 0) ? "ENABLED" : "DISABLED");
                                // synthesis translate_on
                            end
                            // If busy, don't ack
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
                            // Read received data (always from rx_data register - no RX FIFO)
                            mmio_rdata <= {24'h0, rx_data};
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[SPI] RX: 0x%02x", rx_data);
                            // synthesis translate_on
                        end

                        ADDR_SPI_STATUS: begin
                            // Read status register
                            // Bit 0: busy
                            // Bit 1: done (!busy for compatibility)
                            // Bit 2: TX FIFO full
                            // Bit 3: TX FIFO empty
                            // Bits 24-16: TX FIFO level
                            mmio_rdata <= {7'h0, tx_fifo_level[8:0],
                                           12'h0, tx_fifo_empty, tx_fifo_full, ~busy, busy};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_CS: begin
                            // Read chip select state
                            mmio_rdata <= {31'h0, cs_manual};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_XFER_COUNT: begin
                            // Read remaining transfer count
                            mmio_rdata <= {22'h0, xfer_remaining};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_FIFO_STATUS: begin
                            // Read TX FIFO level
                            // Bits 8-0: TX FIFO level (0-512)
                            mmio_rdata <= {23'h0, tx_fifo_level[8:0]};
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
