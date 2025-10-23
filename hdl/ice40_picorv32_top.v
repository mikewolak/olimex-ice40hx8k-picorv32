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
    // Divide 100 MHz crystal by 2 to get 50 MHz system clock (meets timing at 67.84 MHz max)
    reg clk_div = 0;
    always @(posedge EXTCLK) begin
        clk_div <= ~clk_div;
    end
    wire clk = clk_div;

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

    // UART Core (50 MHz clock after divide-by-2)
    uart #(
        .CLK_FREQ(50_000_000),
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
    wire [18:0] sram_addr_16_cpu;
    wire [15:0] sram_wdata_16_cpu;
    wire [15:0] sram_rdata_16;
    wire sram_we_cpu;
    wire sram_valid_16_cpu;
    wire sram_ready_16;

    // SRAM direct connection - no arbiter needed (bootloader handles uploads via CPU)

    // *** CLEAN-ROOM SRAM Driver with COOLDOWN fix ***
    sram_driver_new sram_drv (
        .clk(clk),
        .resetn(global_resetn),
        .valid(sram_valid_16_cpu),
        .ready(sram_ready_16),
        .we(sram_we_cpu),
        .addr(sram_addr_16_cpu),
        .wdata(sram_wdata_16_cpu),
        .rdata(sram_rdata_16),
        .sram_addr(SA),
        .sram_data(SD),
        .sram_cs_n(SRAM_CS_N),
        .sram_oe_n(SRAM_OE_N),
        .sram_we_n(SRAM_WE_N)
    );

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
    wire soft_irq;      // IRQ[1]: Software interrupt / trap / FreeRTOS yield
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

    // SRAM Processor for CPU (via Memory Controller)
    sram_proc_new sram_proc_cpu (
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
        .result_low(),
        .result_high(),
        .rx_byte(8'h00),
        .rx_valid(1'b0),
        .tx_data(),
        .tx_valid(),
        .tx_ready(1'b1),
        .sram_valid(sram_valid_16_cpu),
        .sram_ready(sram_ready_16),
        .sram_we(sram_we_cpu),
        .sram_addr_16(sram_addr_16_cpu),
        .sram_wdata_16(sram_wdata_16_cpu),
        .sram_rdata_16(sram_rdata_16)
    );

    // MMIO output wires (need to MUX between peripherals and SPI)
    wire [31:0] mmio_periph_rdata;
    wire        mmio_periph_ready;
    wire [31:0] spi_rdata;
    wire        spi_ready;

    // Address decode for SPI (0x80000050-0x8000005F)
    wire addr_is_spi = (mmio_addr[31:4] == 28'h8000005);

    // MUX MMIO outputs
    assign mmio_rdata = addr_is_spi ? spi_rdata : mmio_periph_rdata;
    assign mmio_ready = addr_is_spi ? spi_ready : mmio_periph_ready;

    // MMIO Peripherals - UART, LED, Button, and Timer registers
    mmio_peripherals mmio (
        .clk(clk),
        .resetn(cpu_resetn),

        // MMIO Interface
        .mmio_valid(mmio_valid && !addr_is_spi),
        .mmio_write(mmio_write),
        .mmio_addr(mmio_addr),
        .mmio_wdata(mmio_wdata),
        .mmio_wstrb(mmio_wstrb),
        .mmio_rdata(mmio_periph_rdata),
        .mmio_ready(mmio_periph_ready),

        // UART TX Interface
        .uart_tx_data(mmio_uart_tx_data),
        .uart_tx_valid(mmio_uart_tx_valid),
        .uart_tx_busy(uart_tx_busy),

        // UART RX Interface (circular buffer)
        .uart_rx_data(buffer_rd_data),
        .uart_rx_rd_en(mmio_buffer_rd_en),
        .uart_rx_empty(buffer_empty),

        // LED Outputs
        .led1(led1_mmio),
        .led2(led2_mmio),

        // Button Inputs (pre-synchronized)
        .but1_sync(but1_sync2),
        .but2_sync(but2_sync2),

        // Interrupt Outputs
        .timer_irq(timer_irq),
        .soft_irq(soft_irq)
    );

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
