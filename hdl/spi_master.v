//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// spi_master.v - SPI Master with Byte Counter for Burst Transfers
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//
// ENHANCEMENT: Adds burst transfer support via byte counter
// - Minimal LUT overhead (~25 LUTs)
// - No BRAM/FIFO required
// - Firmware manages buffering
// - Expected 2.5x-3x performance improvement
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
    output wire       spi_irq,     // Transfer complete interrupt (single-cycle pulse)

    // DMA Memory Bus (master interface) - NEW
    output reg        dma_mem_valid,
    output reg        dma_mem_write,
    output reg [31:0] dma_mem_addr,
    output reg [31:0] dma_mem_wdata,
    output reg [ 3:0] dma_mem_wstrb,
    input wire [31:0] dma_mem_rdata,
    input wire        dma_mem_ready
);

    //==========================================================================
    // Memory Map (Base: 0x80000050)
    //==========================================================================
    localparam ADDR_SPI_CTRL     = 32'h80000050;  // Control register
    localparam ADDR_SPI_DATA     = 32'h80000054;  // Data register
    localparam ADDR_SPI_STATUS   = 32'h80000058;  // Status register
    localparam ADDR_SPI_CS       = 32'h8000005C;  // Chip select control
    localparam ADDR_SPI_BURST    = 32'h80000060;  // Burst byte count
    localparam ADDR_SPI_DMA_ADDR = 32'h80000064;  // DMA address register (NEW)
    localparam ADDR_SPI_DMA_CTRL = 32'h80000068;  // DMA control register (NEW)

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
    // Burst Transfer Registers
    //==========================================================================
    reg [12:0] burst_count;   // Remaining bytes in burst (0-8192)
    reg        burst_mode;    // 1 = burst active, 0 = single-byte mode

    //==========================================================================
    // DMA Registers (NEW)
    //==========================================================================
    reg [31:0] dma_addr;         // Current SRAM address
    reg [31:0] dma_base_addr;    // Starting SRAM address
    reg        dma_direction;    // 0=TX (SRAM->SPI), 1=RX (SPI->SRAM)
    reg        dma_active;       // DMA engine active
    reg        dma_irq_en;       // Generate IRQ on DMA complete
    reg [7:0]  dma_buffer;       // Single-byte buffer for transfers
    reg        dma_initiated;    // Flag: Current SPI transfer was started by DMA

    // DMA state machine states
    localparam DMA_IDLE      = 4'd0;
    localparam DMA_SETUP     = 4'd1;
    localparam DMA_READ_MEM  = 4'd2;
    localparam DMA_WAIT_MEM  = 4'd3;
    localparam DMA_WRITE_SPI = 4'd4;
    localparam DMA_WAIT_SPI  = 4'd5;
    localparam DMA_READ_SPI  = 4'd6;
    localparam DMA_WRITE_MEM = 4'd7;
    localparam DMA_NEXT      = 4'd8;
    localparam DMA_DONE      = 4'd9;

    reg [3:0] dma_state;

    //==========================================================================
    // IRQ pulse generation - Centralized to avoid multiple drivers
    //==========================================================================
    reg        spi_done_irq;   // Flag: SPI transfer complete (non-DMA)
    reg        dma_done_irq;   // Flag: DMA transfer complete
    reg        irq_pulse;      // Actual IRQ output pulse

    assign spi_irq = irq_pulse;

    //==========================================================================
    // Centralized IRQ Controller - Single source of IRQ generation
    //==========================================================================
    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            irq_pulse <= 1'b0;
        end else begin
            // Default: clear pulse (single-cycle behavior)
            irq_pulse <= 1'b0;

            // Generate IRQ based on flags from state machines
            if (spi_done_irq || dma_done_irq) begin
                irq_pulse <= 1'b1;
            end
        end
    end

    //==========================================================================
    // Centralized Burst Counter Controller - Single source for burst management
    //==========================================================================
    // Signals from state machines to control burst counter
    reg spi_burst_dec;     // SPI requests burst decrement
    reg spi_burst_last;    // SPI signals last byte of manual burst
    reg dma_burst_dec;     // DMA requests burst decrement
    reg dma_burst_done;    // DMA signals burst complete
    reg mmio_burst_set;    // MMIO sets new burst count
    reg [12:0] mmio_burst_value; // New burst count from MMIO

    always @(posedge clk or negedge resetn) begin
        if (!resetn) begin
            burst_count <= 13'h0;
            burst_mode  <= 1'b0;
        end else begin
            // Priority order: MMIO set > DMA done > DMA dec > SPI last > SPI dec
            if (mmio_burst_set) begin
                // MMIO write to burst register
                burst_count <= mmio_burst_value;
                burst_mode  <= (mmio_burst_value != 13'h0);
                // synthesis translate_off
                $display("[BURST_CTL] MMIO_SET: burst_count=%d", mmio_burst_value);
                // synthesis translate_on
            end else if (dma_burst_done) begin
                // DMA transfer complete
                burst_count <= 13'h0;
                burst_mode  <= 1'b0;
                // synthesis translate_off
                $display("[BURST_CTL] DMA_DONE: burst_count=0");
                // synthesis translate_on
            end else if (dma_burst_dec) begin
                // DMA requests decrement
                burst_count <= burst_count - 1'b1;
                // synthesis translate_off
                $display("[BURST_CTL] DMA_DEC: burst_count=%d -> %d", burst_count, burst_count-1);
                // synthesis translate_on
            end else if (spi_burst_last) begin
                // SPI signals last byte of manual burst
                burst_count <= 13'h0;
                burst_mode  <= 1'b0;
                // synthesis translate_off
                $display("[BURST_CTL] SPI_LAST: burst_count=0");
                // synthesis translate_on
            end else if (spi_burst_dec) begin
                // SPI requests decrement (manual burst middle byte)
                burst_count <= burst_count - 1'b1;
                // synthesis translate_off
                $display("[BURST_CTL] SPI_DEC: burst_count=%d -> %d", burst_count, burst_count-1);
                // synthesis translate_on
            end
        end
    end

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
    // SPI State Machine (Enhanced with Burst Support)
    //==========================================================================
    reg sck_phase;  // Internal clock phase tracker
    reg miso_captured;  // Captured MISO bit (for CPHA=0 mode)

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
            miso_captured <= 1'b0;
            spi_done_irq <= 1'b0;
            spi_burst_dec <= 1'b0;
            spi_burst_last <= 1'b0;
            dma_initiated <= 1'b0;
        end else begin
            // Default: Clear all SPI control flags (single-cycle pulses)
            spi_done_irq <= 1'b0;
            spi_burst_dec <= 1'b0;
            spi_burst_last <= 1'b0;

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
                        done <= 1'b0;  // Clear done when starting new transfer
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
                    // Return clock to idle state and save received data
                    spi_sck <= cpol;
                    rx_data <= shift_reg;
                    busy <= 1'b0;
                    done <= 1'b1;  // Signal transfer complete

                    // IRQ and burst management for manual (non-DMA) mode only
                    // Check dma_initiated to distinguish DMA transfers from manual transfers
                    // dma_initiated is set when DMA writes to SPI, cleared when transfer completes
                    if (!dma_initiated) begin
                        if (burst_mode && burst_count == 13'd1) begin
                            // Last byte of manual burst - signal completion
                            spi_burst_last <= 1'b1;
                            spi_done_irq <= 1'b1;
                            // synthesis translate_off
                            $display("[SPI_FINISH] LAST BYTE: dma_initiated=%b burst_count=%d",
                                     dma_initiated, burst_count);
                            // synthesis translate_on
                        end else if (burst_mode && burst_count > 13'd1) begin
                            // Middle of manual burst - request decrement
                            spi_burst_dec <= 1'b1;
                            // synthesis translate_off
                            $display("[SPI_FINISH] MIDDLE: dma_initiated=%b burst_count=%d",
                                     dma_initiated, burst_count);
                            // synthesis translate_on
                        end else if (!burst_mode) begin
                            // Single-byte non-DMA transfer - generate IRQ
                            spi_done_irq <= 1'b1;
                            // synthesis translate_off
                            $display("[SPI_FINISH] SINGLE: dma_initiated=%b burst_count=%d",
                                     dma_initiated, burst_count);
                            // synthesis translate_on
                        end
                    end else if (dma_initiated) begin
                        // synthesis translate_off
                        $display("[SPI_FINISH] DMA: dma_initiated=%b burst_count=%d (no IRQ from SPI)",
                                 dma_initiated, burst_count);
                        // synthesis translate_on
                    end

                    // Clear dma_initiated flag after transfer completes
                    dma_initiated <= 1'b0;

                    // Note: When dma_active=1 or dma_initiated=1, DMA state machine controls burst via flags
                    state <= STATE_IDLE;
                end

                default: state <= STATE_IDLE;
            endcase
        end
    end

    //==========================================================================
    // MMIO Register Interface (Enhanced with Burst Register)
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
            mmio_burst_set <= 1'b0;
            mmio_burst_value <= 13'h0;
        end else begin
            // Clear control signals and flags
            mmio_ready <= 1'b0;
            mmio_burst_set <= 1'b0;

            // Only clear tx_valid if DMA is not active
            // (DMA state machine controls tx_valid when dma_active)
            if (!dma_active) begin
                tx_valid <= 1'b0;
            end

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
                                $display("[SPI] TX: 0x%02x (burst=%d remaining)",
                                         mmio_wdata[7:0], burst_count);
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

                        ADDR_SPI_BURST: begin
                            // Write burst count via centralized controller
                            if (mmio_wstrb[0] || mmio_wstrb[1]) begin
                                mmio_burst_set <= 1'b1;
                                mmio_burst_value <= mmio_wdata[12:0];
                            end
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[SPI] BURST: count=%d mode=%b",
                                     mmio_wdata[12:0], (mmio_wdata[12:0] != 13'h0));
                            // synthesis translate_on
                        end

                        ADDR_SPI_DMA_ADDR: begin
                            // Write DMA address register (NEW)
                            if (mmio_wstrb[0]) dma_base_addr[7:0]   <= mmio_wdata[7:0];
                            if (mmio_wstrb[1]) dma_base_addr[15:8]  <= mmio_wdata[15:8];
                            if (mmio_wstrb[2]) dma_base_addr[23:16] <= mmio_wdata[23:16];
                            if (mmio_wstrb[3]) dma_base_addr[31:24] <= mmio_wdata[31:24];
                            mmio_ready <= 1'b1;

                            // synthesis translate_off
                            $display("[SPI] DMA_ADDR: addr=0x%08x", mmio_wdata);
                            // synthesis translate_on
                        end

                        ADDR_SPI_DMA_CTRL: begin
                            // Write DMA control register (NEW)
                            if (mmio_wstrb[0]) begin
                                dma_direction <= mmio_wdata[1];
                                dma_irq_en    <= mmio_wdata[3];
                                // Start bit (bit 0): triggers DMA if not already active
                                // Note: dma_active is set by DMA state machine in SETUP state
                                if (mmio_wdata[0] && !dma_active && burst_count > 0) begin
                                    dma_state  <= DMA_SETUP;

                                    // synthesis translate_off
                                    $display("[SPI] DMA_CTRL: Starting DMA, dir=%b count=%d",
                                             mmio_wdata[1], burst_count);
                                    // synthesis translate_on
                                end
                            end
                            mmio_ready <= 1'b1;
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
                            // Bit 0: busy
                            // Bit 1: done (!busy)
                            // Bit 2: burst_mode active
                            // Bit 3: dma_active (NEW)
                            mmio_rdata <= {28'h0, dma_active, burst_mode, ~busy, busy};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_CS: begin
                            // Read chip select state
                            mmio_rdata <= {31'h0, cs_manual};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_BURST: begin
                            // Read burst count remaining (NEW)
                            mmio_rdata <= {19'h0, burst_count};
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_DMA_ADDR: begin
                            // Read DMA address register (NEW)
                            mmio_rdata <= dma_base_addr;
                            mmio_ready <= 1'b1;
                        end

                        ADDR_SPI_DMA_CTRL: begin
                            // Read DMA control register (NEW)
                            // Bit 0: start (write-only, always reads 0)
                            // Bit 1: direction (0=TX, 1=RX)
                            // Bit 2: busy (dma_active)
                            // Bit 3: irq_en
                            mmio_rdata <= {28'h0, dma_irq_en, dma_active, dma_direction, 1'b0};
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

    //==========================================================================
    // DMA Engine State Machine
    //==========================================================================
    always @(posedge clk) begin
        if (!resetn) begin
            dma_state     <= DMA_IDLE;
            dma_active    <= 1'b0;
            dma_addr      <= 32'h0;
            dma_buffer    <= 8'h0;
            dma_mem_valid <= 1'b0;
            dma_mem_write <= 1'b0;
            dma_mem_addr  <= 32'h0;
            dma_mem_wdata <= 32'h0;
            dma_mem_wstrb <= 4'h0;
            dma_done_irq  <= 1'b0;
            dma_burst_dec  <= 1'b0;
            dma_burst_done <= 1'b0;
        end else begin
            // Default: Clear all DMA control flags (single-cycle pulses)
            dma_done_irq   <= 1'b0;
            dma_burst_dec  <= 1'b0;
            dma_burst_done <= 1'b0;

            // Default: clear memory bus valid when transaction completes
            if (dma_mem_ready) begin
                dma_mem_valid <= 1'b0;
            end

            case (dma_state)
                DMA_IDLE: begin
                    // Waiting for start trigger (handled in MMIO write)
                    dma_active <= 1'b0;
                end

                DMA_SETUP: begin
                    // Initialize DMA transfer and activate DMA engine
                    dma_active <= 1'b1;
                    dma_addr  <= dma_base_addr;
                    if (dma_direction == 1'b0) begin
                        // TX mode: read from SRAM first
                        dma_state <= DMA_READ_MEM;
                    end else begin
                        // RX mode: start SPI transfer first (send dummy byte)
                        dma_state <= DMA_WRITE_SPI;
                    end

                    // synthesis translate_off
                    $display("[SPI_DMA] SETUP: addr=0x%08x dir=%s count=%d",
                             dma_base_addr, dma_direction ? "RX" : "TX", burst_count);
                    // synthesis translate_on
                end

                DMA_READ_MEM: begin
                    // Request byte from SRAM
                    if (!dma_mem_valid) begin
                        dma_mem_valid <= 1'b1;
                        dma_mem_write <= 1'b0;
                        dma_mem_addr  <= dma_addr;
                        dma_mem_wstrb <= 4'b0000;
                        dma_state     <= DMA_WAIT_MEM;
                    end
                end

                DMA_WAIT_MEM: begin
                    if (dma_mem_ready) begin
                        // Store byte from memory or complete write
                        if (!dma_mem_write) begin
                            // Read completed
                            dma_buffer    <= dma_mem_rdata[7:0];
                            dma_mem_valid <= 1'b0;

                            // synthesis translate_off
                            $display("[SPI_DMA] READ_MEM: addr=0x%08x data=0x%02x",
                                     dma_addr, dma_mem_rdata[7:0]);
                            // synthesis translate_on

                            if (dma_direction == 1'b0) begin
                                // TX: write to SPI next
                                dma_state <= DMA_WRITE_SPI;
                            end else begin
                                // RX: shouldn't happen in this state
                                dma_state <= DMA_IDLE;
                            end
                        end else begin
                            // Write completed
                            dma_mem_valid <= 1'b0;

                            // synthesis translate_off
                            $display("[SPI_DMA] WRITE_MEM: addr=0x%08x data=0x%02x",
                                     dma_addr, dma_buffer);
                            // synthesis translate_on

                            // Move to next byte
                            dma_state <= DMA_NEXT;
                        end
                    end
                end

                DMA_WRITE_SPI: begin
                    // Write byte to SPI core (use existing tx_data/tx_valid mechanism)
                    if (!busy && !tx_valid) begin
                        if (dma_direction == 1'b0) begin
                            // TX mode: send data from buffer
                            tx_data   <= dma_buffer;
                        end else begin
                            // RX mode: send dummy byte
                            tx_data   <= 8'hFF;
                        end
                        tx_valid      <= 1'b1;
                        dma_initiated <= 1'b1;  // Mark this SPI transfer as DMA-initiated
                        dma_state     <= DMA_WAIT_SPI;

                        // synthesis translate_off
                        $display("[SPI_DMA] WRITE_SPI: data=0x%02x (dir=%s)",
                                 dma_direction ? 8'hFF : dma_buffer,
                                 dma_direction ? "RX" : "TX");
                        // synthesis translate_on
                    end
                end

                DMA_WAIT_SPI: begin
                    // Wait for transfer done
                    if (done) begin
                        // Clear tx_valid for next transfer
                        tx_valid <= 1'b0;

                        if (dma_direction == 1'b0) begin
                            // TX: done with this byte
                            dma_state <= DMA_NEXT;
                        end else begin
                            // RX: read received byte
                            dma_state <= DMA_READ_SPI;
                        end
                    end
                end

                DMA_READ_SPI: begin
                    // Read byte from SPI core
                    dma_buffer <= rx_data;
                    dma_state  <= DMA_WRITE_MEM;

                    // synthesis translate_off
                    $display("[SPI_DMA] READ_SPI: data=0x%02x", rx_data);
                    // synthesis translate_on
                end

                DMA_WRITE_MEM: begin
                    // Write byte to SRAM
                    if (!dma_mem_valid) begin
                        dma_mem_valid <= 1'b1;
                        dma_mem_write <= 1'b1;
                        dma_mem_addr  <= dma_addr;
                        dma_mem_wdata <= {24'h0, dma_buffer};
                        dma_mem_wstrb <= 4'b0001;  // Byte write (lane 0)
                        dma_state     <= DMA_WAIT_MEM;
                    end
                end

                DMA_NEXT: begin
                    // Increment address
                    dma_addr <= dma_addr + 1'b1;

                    // synthesis translate_off
                    $display("[SPI_DMA] NEXT: addr=0x%08x count=%d", dma_addr, burst_count);
                    // synthesis translate_on

                    if (burst_count > 1) begin
                        // More bytes to transfer - request decrement via centralized controller
                        dma_burst_dec <= 1'b1;

                        if (dma_direction == 1'b0) begin
                            // TX: read next byte from memory
                            dma_state <= DMA_READ_MEM;
                        end else begin
                            // RX: start next SPI transfer
                            dma_state <= DMA_WRITE_SPI;
                        end
                    end else begin
                        // Transfer complete - signal burst done via centralized controller
                        dma_burst_done <= 1'b1;
                        dma_state <= DMA_DONE;

                        // synthesis translate_off
                        $display("[SPI_DMA] NEXT: Transfer complete");
                        // synthesis translate_on
                    end
                end

                DMA_DONE: begin
                    // Note: dma_active is cleared in IDLE state (next clock)
                    // This keeps it active until after the last SPI transfer completes
                    if (dma_irq_en) begin
                        dma_done_irq <= 1'b1;  // Set flag for centralized IRQ controller

                        // synthesis translate_off
                        $display("[SPI_DMA] DONE: IRQ generated");
                        // synthesis translate_on
                    end
                    dma_state <= DMA_IDLE;
                end

                default: dma_state <= DMA_IDLE;
            endcase
        end
    end

endmodule
