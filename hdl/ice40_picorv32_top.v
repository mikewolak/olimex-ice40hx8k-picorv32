//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// ice40_picorv32_top.v - Top-Level FPGA Design
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

module ice40_picorv32_top (
    // Clock and Reset
    input wire EXTCLK,          // 100MHz external clock (J3)

    // User Interface
    input wire BUT1,            // User button 1 (K11)
    input wire BUT2,            // User button 2 (P13)
    output wire LED1,           // User LED 1 (M12)
    output wire LED2,           // User LED 2 (R16)

    // UART Interface
    input wire UART_RX,         // UART Receive (E4)
    output wire UART_TX,        // UART Transmit (B2)

    // SPI Interface
    output wire SPI_SCK,        // SPI Clock (F5)
    output wire SPI_MOSI,       // SPI Master Out Slave In (B1)
    input wire SPI_MISO,        // SPI Master In Slave Out (C1)
    output wire SPI_CS,         // SPI Chip Select (C2)

    // SRAM Interface (K6R4016V1D-TC10)
    output wire [17:0] SA,      // SRAM Address bus
    inout wire [15:0] SD,       // SRAM Data bus
    output wire SRAM_CS_N,      // SRAM Chip Select (active low)
    output wire SRAM_OE_N,      // SRAM Output Enable (active low)
    output wire SRAM_WE_N       // SRAM Write Enable (active low)
);

    // Clock and reset management
    // Use PLL to generate 62.5 MHz from 100 MHz input (closest to requested 60 MHz)
    // Note: Exact 60 MHz not achievable with integer PLL dividers
    // 62.5 MHz is 0.3% over 62.31 MHz timing limit but closest to target
    wire clk;           // 62.5 MHz system clock from PLL
    wire pll_locked;    // PLL lock indicator

    SB_PLL40_CORE #(
        .FEEDBACK_PATH("SIMPLE"),
        .DIVR(4'b0000),         // DIVR = 0  (input divider = DIVR+1 = 1)
        .DIVF(7'b0010011),      // DIVF = 19 (feedback multiplier = DIVF+1 = 20)
        .DIVQ(3'b101),          // DIVQ = 5  (output divider = 2^DIVQ = 32)
        .FILTER_RANGE(3'b001)   // Filter for 10-40 MHz input range
    ) pll_inst (
        .REFERENCECLK(EXTCLK),  // 100 MHz input
        .PLLOUTCORE(clk),       // 62.5 MHz output: 100 * 20 / 32 = 62.5
        .LOCK(pll_locked),      // PLL locked signal
        .RESETB(1'b1),          // PLL always enabled
        .BYPASS(1'b0)           // PLL not bypassed
    );

    reg [7:0] reset_counter = 0;
    wire global_resetn = &reset_counter;

    always @(posedge clk) begin
        if (!global_resetn)
            reset_counter <= reset_counter + 1;
    end

    // CPU reset tied to global reset
    // Bootloader ROM is BRAM initialized at synthesis time, so no boot delay needed
    wire cpu_resetn = global_resetn;

    // Button synchronizers (2-stage, using global reset, not cpu reset)
    // Active-low buttons inverted to active-high (1 = pressed)
    reg but1_sync1, but1_sync2;
    reg but2_sync1, but2_sync2;

    always @(posedge clk) begin
        if (!global_resetn) begin
            but1_sync1 <= 1'b0;
            but1_sync2 <= 1'b0;
            but2_sync1 <= 1'b0;
            but2_sync2 <= 1'b0;
        end else begin
            but1_sync1 <= ~BUT1;  // Invert active-low to active-high
            but1_sync2 <= but1_sync1;
            but2_sync1 <= ~BUT2;
            but2_sync2 <= but2_sync1;
        end
    end

    // LED Control - direct from MMIO (no shell/app mode switching)
    wire led1_mmio, led2_mmio;
    assign LED1 = led1_mmio;
    assign LED2 = led2_mmio;

    // UART signals
    wire [7:0] uart_rx_data;
    wire uart_rx_data_valid;
    wire uart_tx_busy, uart_rx_busy, uart_rx_error;

    // Forward declarations for UART TX
    // Bootloader handles all uploads - MMIO provides UART interface
    wire [7:0] mmio_uart_tx_data;
    wire mmio_uart_tx_valid;

    // UART TX direct from MMIO (no multiplexing needed)
    wire [7:0] uart_tx_data_mux = mmio_uart_tx_data;
    wire uart_tx_valid_mux = mmio_uart_tx_valid;

    // UART Core (62.5 MHz clock from PLL)
    uart #(
        .CLK_FREQ(62_500_000),
        .BAUD_RATE(1_000_000),  // 1 Mbaud for FAST streaming
        .OS_RATE(16),
        .D_WIDTH(8),
        .PARITY(0),
        .PARITY_EO(1'b0)
    ) uart_core (
        .clk(clk),
        .reset_n(global_resetn),
        .tx_ena(uart_tx_valid_mux),
        .tx_data(uart_tx_data_mux),
        .rx(UART_RX),
        .rx_busy(uart_rx_busy),
        .rx_error(uart_rx_error),
        .rx_data(uart_rx_data),
        .rx_data_valid(uart_rx_data_valid),
        .tx_busy(uart_tx_busy),
        .tx(UART_TX)
    );

    // Circular Buffer for UART RX
    // UART RX Circular Buffer
    // Buffer shared between bootloader and application UART access
    wire mmio_buffer_rd_en;
    wire [7:0] buffer_rd_data;
    wire buffer_full, buffer_empty;
    wire buffer_wr_en = uart_rx_data_valid && !buffer_full;
    wire buffer_rd_en = mmio_buffer_rd_en;

    circular_buffer #(
        .DATA_WIDTH(8),
        .ADDR_BITS(8)  // 256 bytes buffer (increased from 64)
    ) uart_circular_buffer (
        .clk(clk),
        .reset_n(global_resetn),
        .clear(1'b0),  // No buffer clear needed without shell
        .wr_en(buffer_wr_en),
        .wr_data(uart_rx_data),
        .full(buffer_full),
        .rd_en(buffer_rd_en),
        .rd_data(buffer_rd_data),
        .empty(buffer_empty)
    );

    // SRAM 16-bit driver interface
    // No shell anymore - only firmware loader and CPU
    // NOTE: Using unified SRAM controller (optimized, 4-7 cycle access)
    // Old 3-layer design removed: sram_driver_new + sram_proc_new
    // (can revert to v0.12-baseline-tests tag if needed)

    // ========================================
    // PicoRV32 CPU + Memory-Mapped I/O
    // ========================================

    // PicoRV32 Memory Interface
    wire        cpu_mem_valid;
    wire        cpu_mem_instr;
    wire        cpu_mem_ready;
    wire [31:0] cpu_mem_addr;
    wire [31:0] cpu_mem_wdata;
    wire [ 3:0] cpu_mem_wstrb;
    wire [31:0] cpu_mem_rdata;

    // Interrupt signals from peripherals
    wire timer_irq;     // IRQ[0]: Timer periodic tick (100 Hz)
    reg soft_irq;       // IRQ[1]: Software interrupt / trap / FreeRTOS yield
    wire spi_irq;       // IRQ[2]: SPI transfer complete

    // PicoRV32 CPU Core - RV32I (32 regs) with MUL/DIV, barrel shifter, and interrupts
    // Boots from bootloader at 0x40000, which then jumps to firmware at 0x0
    picorv32 #(
        .ENABLE_COUNTERS(0),
        .ENABLE_COUNTERS64(0),
        .ENABLE_REGS_16_31(1),          // RV32I: full 32 registers (x0-x31)
        .ENABLE_REGS_DUALPORT(0),
        .LATCHED_MEM_RDATA(0),
        .TWO_STAGE_SHIFT(0),            // Disable slow shifter when using barrel shifter
        .BARREL_SHIFTER(1),             // Fast single-cycle shifts
        .TWO_CYCLE_COMPARE(0),
        .TWO_CYCLE_ALU(0),
        .COMPRESSED_ISA(0),
        .CATCH_MISALIGN(0),
        .CATCH_ILLINSN(0),
        .ENABLE_PCPI(0),
        .ENABLE_MUL(1),                 // Enable multiply instructions
        .ENABLE_FAST_MUL(0),
        .ENABLE_DIV(1),                 // Enable divide instructions
        .ENABLE_IRQ(1),                 // Enable interrupt support
        .ENABLE_IRQ_QREGS(1),           // Enable IRQ shadow registers (q0-q3)
        .ENABLE_IRQ_TIMER(1),           // Enable IRQ timer register
        .ENABLE_TRACE(0),
        .REGS_INIT_ZERO(1),
        .MASKED_IRQ(32'h00000000),
        .LATCHED_IRQ(32'hffffffff),
`ifdef SIMULATION
        .PROGADDR_RESET(32'h00000000),  // SIMULATION: Start from SRAM (firmware pre-loaded)
`else
        .PROGADDR_RESET(32'h00040000),  // HARDWARE: Start from bootloader ROM
`endif
        .PROGADDR_IRQ(32'h00000010),    // IRQ handler at 0x10
        .STACKADDR(32'h00080000)
    ) cpu (
        .clk(clk),
        .resetn(cpu_resetn),
        .trap(),

        .mem_valid(cpu_mem_valid),
        .mem_instr(cpu_mem_instr),
        .mem_ready(cpu_mem_ready),
        .mem_addr(cpu_mem_addr),
        .mem_wdata(cpu_mem_wdata),
        .mem_wstrb(cpu_mem_wstrb),
        .mem_rdata(cpu_mem_rdata),

        .mem_la_read(),
        .mem_la_write(),
        .mem_la_addr(),
        .mem_la_wdata(),
        .mem_la_wstrb(),

        .pcpi_valid(),
        .pcpi_insn(),
        .pcpi_rs1(),
        .pcpi_rs2(),
        .pcpi_wr(1'b0),
        .pcpi_rd(32'h0),
        .pcpi_wait(1'b0),
        .pcpi_ready(1'b0),

        .irq({29'h0, spi_irq, soft_irq, timer_irq}),  // IRQ[2]=SPI, IRQ[1]=software, IRQ[0]=timer
        .eoi()  // EOI not used
    );

    // Bootloader ROM signals
    wire        boot_enable;
    wire [12:0] boot_addr;
    wire [31:0] boot_rdata;

    // Bootloader ROM - 8KB BRAM at 0x40000
    // Initialized from bootloader.hex at synthesis time via $readmemh
    bootloader_rom boot_rom (
        .clk(clk),
        .resetn(cpu_resetn),
        .addr(boot_addr),
        .enable(boot_enable),
        .rdata(boot_rdata)
    );

    // Memory Controller signals
    wire        mem_ctrl_sram_start;
    wire        mem_ctrl_sram_busy;
    wire        mem_ctrl_sram_done;
    wire [ 7:0] mem_ctrl_sram_cmd;
    wire [31:0] mem_ctrl_sram_addr;
    wire [31:0] mem_ctrl_sram_wdata;
    wire [ 3:0] mem_ctrl_sram_wstrb;
    wire [31:0] mem_ctrl_sram_rdata;

    // MMIO signals
    wire        mmio_valid;
    wire        mmio_write;
    wire [31:0] mmio_addr;
    wire [31:0] mmio_wdata;
    wire [ 3:0] mmio_wstrb;
    wire [31:0] mmio_rdata;
    wire        mmio_ready;

    // Memory Controller - Routes CPU to SRAM, Bootloader ROM, or MMIO
    mem_controller mem_ctrl (
        .clk(clk),
        .resetn(cpu_resetn),

        // PicoRV32 Interface
        .cpu_mem_valid(cpu_mem_valid),
        .cpu_mem_instr(cpu_mem_instr),
        .cpu_mem_ready(cpu_mem_ready),
        .cpu_mem_addr(cpu_mem_addr),
        .cpu_mem_wdata(cpu_mem_wdata),
        .cpu_mem_wstrb(cpu_mem_wstrb),
        .cpu_mem_rdata(cpu_mem_rdata),

        // Bootloader ROM Interface (read-only)
        .boot_enable(boot_enable),
        .boot_addr(boot_addr),
        .boot_rdata(boot_rdata),

        // SRAM Interface (via sram_proc_new)
        .sram_start(mem_ctrl_sram_start),
        .sram_busy(mem_ctrl_sram_busy),
        .sram_done(mem_ctrl_sram_done),
        .sram_cmd(mem_ctrl_sram_cmd),
        .sram_addr(mem_ctrl_sram_addr),
        .sram_wdata(mem_ctrl_sram_wdata),
        .sram_wstrb(mem_ctrl_sram_wstrb),
        .sram_rdata(mem_ctrl_sram_rdata),

        // MMIO Interface
        .mmio_valid(mmio_valid),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(mmio_rdata),
        .mmio_ready(mmio_ready)
    );

    // Unified SRAM Controller (via adapter for mem_controller compatibility)
    sram_unified_adapter sram_unified (
        .clk(clk),
        .resetn(cpu_resetn),
        .start(mem_ctrl_sram_start),
        .cmd(mem_ctrl_sram_cmd),
        .addr_in(mem_ctrl_sram_addr),
        .data_in(mem_ctrl_sram_wdata),
        .mem_wstrb(mem_ctrl_sram_wstrb),
        .busy(mem_ctrl_sram_busy),
        .done(mem_ctrl_sram_done),
        .result(mem_ctrl_sram_rdata),
        .sram_addr(SA),
        .sram_data(SD),
        .sram_cs_n(SRAM_CS_N),
        .sram_oe_n(SRAM_OE_N),
        .sram_we_n(SRAM_WE_N)
    );

    //==========================================================================
    // MMIO Address Decode
    //==========================================================================
    localparam ADDR_LED_CONTROL  = 32'h80000010;
    localparam ADDR_BUTTON_INPUT = 32'h80000018;
    localparam ADDR_SOFT_IRQ_W   = 32'h80000040;

    wire addr_is_uart     = (mmio_addr[31:4] == 28'h8000000);  // 0x80000000-0x8000000F
    wire addr_is_simple   = (mmio_addr == ADDR_LED_CONTROL) ||
                            (mmio_addr == ADDR_BUTTON_INPUT) ||
                            (mmio_addr == ADDR_SOFT_IRQ_W);
    wire addr_is_timer    = (mmio_addr[31:4] == 28'h8000002);  // 0x80000020-0x8000002F
    wire addr_is_spi      = (mmio_addr[31:4] == 28'h8000005);  // 0x80000050-0x8000005F

    //==========================================================================
    // Simple I/O Peripheral (LED, Button, Soft IRQ)
    //==========================================================================
    reg [1:0]  led_reg;
    reg [31:0] simple_io_rdata;
    reg        simple_io_ready;

    always @(posedge clk) begin
        if (!cpu_resetn) begin
            led_reg <= 2'b00;
            simple_io_ready <= 1'b0;
            soft_irq <= 1'b0;
        end else begin
            simple_io_ready <= 1'b0;
            soft_irq <= 1'b0;  // Single-cycle pulse

            if (mmio_valid && addr_is_simple && !simple_io_ready) begin
                if (mmio_write) begin
                    case (mmio_addr)
                        ADDR_LED_CONTROL: begin
                            if (mmio_wstrb[0]) led_reg <= mmio_wdata[1:0];
                            simple_io_ready <= 1'b1;
                        end
                        ADDR_SOFT_IRQ_W: begin
                            soft_irq <= 1'b1;
                            simple_io_ready <= 1'b1;
                        end
                        default: simple_io_ready <= 1'b1;
                    endcase
                end else begin
                    case (mmio_addr)
                        ADDR_LED_CONTROL:  simple_io_rdata <= {30'h0, led_reg};
                        ADDR_BUTTON_INPUT: simple_io_rdata <= {30'h0, but2_sync2, but1_sync2};
                        default:           simple_io_rdata <= 32'h0;
                    endcase
                    simple_io_ready <= 1'b1;
                end
            end
        end
    end

    assign led1_mmio = led_reg[0];
    assign led2_mmio = led_reg[1];

    //==========================================================================
    // UART Peripheral
    //==========================================================================
    wire [31:0] uart_rdata;
    wire        uart_ready;

    uart_peripheral uart_periph (
        .clk(clk),
        .resetn(cpu_resetn),
        .mmio_valid(mmio_valid && addr_is_uart),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(uart_rdata),
        .mmio_ready(uart_ready),
        .uart_tx_data(mmio_uart_tx_data),
        .uart_tx_valid(mmio_uart_tx_valid),
        .uart_tx_busy(uart_tx_busy),
        .uart_rx_data(buffer_rd_data),
        .uart_rx_rd_en(mmio_buffer_rd_en),
        .uart_rx_empty(buffer_empty)
    );

    //==========================================================================
    // Timer Peripheral
    //==========================================================================
    wire [31:0] timer_rdata;
    wire        timer_ready;

    timer_peripheral timer (
        .clk(clk),
        .resetn(cpu_resetn),
        .mmio_valid(mmio_valid && addr_is_timer),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(timer_rdata),
        .mmio_ready(timer_ready),
        .timer_irq(timer_irq)
    );

    //==========================================================================
    // MMIO Multiplexer (4-way: simple_io, uart, timer, spi)
    //==========================================================================
    wire [31:0] spi_rdata;
    wire        spi_ready;

    assign mmio_rdata = addr_is_simple ? simple_io_rdata :
                        addr_is_uart   ? uart_rdata :
                        addr_is_timer  ? timer_rdata :
                        addr_is_spi    ? spi_rdata : 32'h0;

    assign mmio_ready = addr_is_simple ? simple_io_ready :
                        addr_is_uart   ? uart_ready :
                        addr_is_timer  ? timer_ready :
                        addr_is_spi    ? spi_ready : 1'b0;

    // SPI Master Peripheral Instance (at top level for better optimization)
    spi_master spi (
        .clk(clk),
        .resetn(cpu_resetn),
        .mmio_valid(mmio_valid && addr_is_spi),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(spi_rdata),
        .mmio_ready(spi_ready),
        .spi_sck(SPI_SCK),
        .spi_mosi(SPI_MOSI),
        .spi_miso(SPI_MISO),
        .spi_cs(SPI_CS),
        .spi_irq(spi_irq)
    );

endmodule
