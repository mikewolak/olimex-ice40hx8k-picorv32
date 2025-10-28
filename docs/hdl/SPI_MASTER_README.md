# SPI Master Peripheral

## Overview

Minimal, gate-efficient SPI master peripheral for PicoRV32 platform. Designed for minimal FPGA resource usage while providing full-featured SPI master functionality with configurable clock speeds.

## Features

- **Configurable Clock Speeds**: 50 MHz down to 390 kHz (÷1, ÷2, ÷4, ÷8, ÷16, ÷32, ÷64, ÷128)
- **SD Card Compatible**: 390 kHz initialization speed meets SD card ≤400 kHz requirement
- **Full SPI Mode Support**: CPOL and CPHA configurable (Modes 0-3)
- **Manual Chip Select**: Software-controlled CS for multi-slave support
- **Simple Interface**: Memory-mapped registers at 0x80000050-0x8000005F
- **Gate-Efficient Design**: ~200 LUTs estimated (minimal resource usage)

## Memory Map

| Address      | Register    | Access | Description                           |
|--------------|-------------|--------|---------------------------------------|
| 0x80000050   | SPI_CTRL    | R/W    | Control register (CPOL, CPHA, CLK_DIV)|
| 0x80000054   | SPI_DATA    | R/W    | Data register (TX/RX)                 |
| 0x80000058   | SPI_STATUS  | R      | Status register (BUSY, DONE)          |
| 0x8000005C   | SPI_CS      | R/W    | Chip select control                   |

## Register Definitions

### SPI_CTRL (0x80000050) - Control Register

| Bits  | Name    | Description                                    |
|-------|---------|------------------------------------------------|
| 0     | CPOL    | Clock polarity (0=idle low, 1=idle high)       |
| 1     | CPHA    | Clock phase (0=sample leading, 1=sample trail) |
| 4:2   | CLK_DIV | Clock divider (000-111, see table below)       |
| 31:5  | -       | Reserved (read as 0)                           |

**Clock Divider Settings (CLK_DIV[2:0]):**
- `000` - 50.0 MHz (÷1) - Maximum speed
- `001` - 25.0 MHz (÷2) - High speed
- `010` - 12.5 MHz (÷4) - Medium speed
- `011` - 6.25 MHz (÷8) - Medium-low speed
- `100` - 3.125 MHz (÷16) - Low speed
- `101` - 1.562 MHz (÷32) - Very low speed
- `110` - 781 kHz (÷64) - Initialization speed
- `111` - 390 kHz (÷128) - SD card initialization (default, safest)

**SPI Modes (CPOL/CPHA combinations):**
- Mode 0: CPOL=0, CPHA=0 (most common)
- Mode 1: CPOL=0, CPHA=1
- Mode 2: CPOL=1, CPHA=0
- Mode 3: CPOL=1, CPHA=1

### SPI_DATA (0x80000054) - Data Register

| Bits  | Name | Description                                |
|-------|------|--------------------------------------------|
| 7:0   | DATA | Data byte (write=transmit, read=receive)  |
| 31:8  | -    | Reserved (read as 0)                       |

**Write Behavior:**
- Writing initiates an SPI transfer
- Transfer is rejected if BUSY=1 (CPU must poll STATUS first)
- Simultaneous transmit and receive (full duplex)

**Read Behavior:**
- Returns last received byte
- Valid after DONE flag is set in STATUS register

### SPI_STATUS (0x80000058) - Status Register

| Bits  | Name | Description                                 |
|-------|------|---------------------------------------------|
| 0     | BUSY | Transfer in progress (1=busy, 0=ready)      |
| 1     | DONE | Transfer complete flag (1=done, 0=pending)  |
| 31:2  | -    | Reserved (read as 0)                        |

**BUSY Flag:**
- Set when transfer starts (write to SPI_DATA)
- Cleared when transfer completes
- CPU must poll BUSY=0 before initiating new transfer

**DONE Flag:**
- Set when transfer completes
- Automatically cleared when STATUS register is read
- Indicates RX_DATA is valid

### SPI_CS (0x8000005C) - Chip Select Control

| Bits  | Name      | Description                           |
|-------|-----------|---------------------------------------|
| 0     | CS_MANUAL | Chip select state (0=active, 1=idle)  |
| 31:1  | -         | Reserved (read as 0)                  |

**CS_MANUAL:**
- Directly controls the `spi_cs` output pin
- 0 = CS asserted (active low)
- 1 = CS deasserted (inactive high)
- Default: 1 (inactive)

**Multi-Slave Support:**
- Use external decoder/mux with additional GPIO pins for multiple slaves
- Or use separate CS GPIO pins per slave

## Usage Examples

### Example 1: SD Card Initialization Sequence

```c
#define SPI_CTRL   (*(volatile uint32_t*)0x80000050)
#define SPI_DATA   (*(volatile uint32_t*)0x80000054)
#define SPI_STATUS (*(volatile uint32_t*)0x80000058)
#define SPI_CS     (*(volatile uint32_t*)0x8000005C)

#define SPI_STATUS_BUSY (1 << 0)
#define SPI_STATUS_DONE (1 << 1)

// Clock divider values
#define SPI_CLK_50MHZ   (0 << 2)  // 000 = 50 MHz
#define SPI_CLK_25MHZ   (1 << 2)  // 001 = 25 MHz
#define SPI_CLK_12MHZ   (2 << 2)  // 010 = 12.5 MHz
#define SPI_CLK_390KHZ  (7 << 2)  // 111 = 390 kHz

void spi_init(void) {
    // Configure: Mode 0 (CPOL=0, CPHA=0), 390 kHz for SD card init
    SPI_CTRL = SPI_CLK_390KHZ | (0 << 1) | (0 << 0);

    // CS inactive (high)
    SPI_CS = 1;
}

uint8_t spi_transfer(uint8_t data) {
    // Wait if busy
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Initiate transfer
    SPI_DATA = data;

    // Wait for completion
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Read received data (also clears DONE flag)
    uint32_t status = SPI_STATUS;  // Clear DONE flag
    return (uint8_t)SPI_DATA;
}

void sd_card_init(void) {
    // Step 1: Send 74+ dummy clocks with CS high (SD card power-up requirement)
    SPI_CS = 1;
    for (int i = 0; i < 10; i++) {
        spi_transfer(0xFF);  // 10 bytes = 80 clocks
    }

    // Step 2: Send CMD0 (GO_IDLE_STATE) with CS low
    SPI_CS = 0;
    spi_transfer(0x40);  // CMD0
    spi_transfer(0x00);  // ARG[31:24]
    spi_transfer(0x00);  // ARG[23:16]
    spi_transfer(0x00);  // ARG[15:8]
    spi_transfer(0x00);  // ARG[7:0]
    spi_transfer(0x95);  // CRC (valid for CMD0)

    uint8_t r1 = spi_transfer(0xFF);  // Read R1 response
    SPI_CS = 1;

    // Step 3: After initialization complete, switch to high speed
    SPI_CTRL = SPI_CLK_25MHZ;  // 25 MHz for normal operation
}

void spi_write_byte(uint8_t data) {
    SPI_CS = 0;           // Assert CS
    spi_transfer(data);   // Send byte
    SPI_CS = 1;           // Deassert CS
}
```

### Example 2: High-Speed Transfer (50 MHz)

```c
void spi_init_fast(void) {
    // Configure: Mode 0, 50 MHz (÷1)
    SPI_CTRL = (0 << 2) | (0 << 1) | (0 << 0);
    SPI_CS = 1;
}

void spi_write_buffer(const uint8_t *buf, size_t len) {
    SPI_CS = 0;  // Assert CS

    for (size_t i = 0; i < len; i++) {
        while (SPI_STATUS & SPI_STATUS_BUSY);
        SPI_DATA = buf[i];
    }

    // Wait for last byte
    while (SPI_STATUS & SPI_STATUS_BUSY);

    SPI_CS = 1;  // Deassert CS
}
```

### Example 3: SPI Flash Read (Mode 3, 25 MHz)

```c
#define CMD_READ 0x03

void spi_flash_init(void) {
    // Configure: Mode 3 (CPOL=1, CPHA=1), 25 MHz
    SPI_CTRL = (1 << 2) | (1 << 1) | (1 << 0);
    SPI_CS = 1;
}

void spi_flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    SPI_CS = 0;  // Assert CS

    // Send READ command
    spi_transfer(CMD_READ);

    // Send 24-bit address
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);

    // Read data
    for (size_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(0x00);
    }

    SPI_CS = 1;  // Deassert CS
}
```

## Hardware Integration

### Required Changes to System

1. **Add to `mmio_peripherals.v`:**

```verilog
// SPI interface signals
output wire       spi_sck,
output wire       spi_mosi,
input  wire       spi_miso,
output wire       spi_cs,

// Inside module:
wire        spi_valid;
wire        spi_ready;
wire [31:0] spi_rdata;

// Address decode for SPI (0x80000050-0x8000005F)
wire addr_is_spi = (mmio_addr[31:4] == 28'h8000005);

// SPI Master Instance
spi_master spi (
    .clk(clk),
    .resetn(resetn),
    .mmio_valid(spi_valid),
    .mmio_write(mmio_write),
    .mmio_addr(mmio_addr),
    .mmio_wdata(mmio_wdata),
    .mmio_wstrb(mmio_wstrb),
    .mmio_rdata(spi_rdata),
    .mmio_ready(spi_ready),
    .spi_sck(spi_sck),
    .spi_mosi(spi_mosi),
    .spi_miso(spi_miso),
    .spi_cs(spi_cs)
);

assign spi_valid = mmio_valid && addr_is_spi;

// In MMIO routing logic:
if (addr_is_spi) begin
    mmio_rdata <= spi_rdata;
    mmio_ready <= spi_ready;
end
```

2. **Add to `ice40_picorv32_top.v`:**

```verilog
// SPI signals
wire spi_sck;
wire spi_mosi;
wire spi_miso;
wire spi_cs;

// Connect to PMOD or expansion header
assign PMOD[0] = spi_sck;
assign PMOD[1] = spi_mosi;
assign spi_miso = PMOD[2];
assign PMOD[3] = spi_cs;

// Pass to mmio_peripherals:
mmio_peripherals mmio (
    ...
    .spi_sck(spi_sck),
    .spi_mosi(spi_mosi),
    .spi_miso(spi_miso),
    .spi_cs(spi_cs),
    ...
);
```

3. **Pin Constraints (.pcf file):**

```
# SPI on PMOD connector (example)
set_io spi_sck  PMOD1_1
set_io spi_mosi PMOD1_2
set_io spi_miso PMOD1_3
set_io spi_cs   PMOD1_4
```

## Timing Characteristics

### Clock Frequencies and Transfer Rates
| Divider | Frequency | Period/bit | Time/byte | Max Throughput |
|---------|-----------|------------|-----------|----------------|
| ÷1      | 50.0 MHz  | 20 ns      | 160 ns    | 6.25 MB/s      |
| ÷2      | 25.0 MHz  | 40 ns      | 320 ns    | 3.125 MB/s     |
| ÷4      | 12.5 MHz  | 80 ns      | 640 ns    | 1.56 MB/s      |
| ÷8      | 6.25 MHz  | 160 ns     | 1.28 μs   | 781 kB/s       |
| ÷16     | 3.125 MHz | 320 ns     | 2.56 μs   | 390 kB/s       |
| ÷32     | 1.562 MHz | 640 ns     | 5.12 μs   | 195 kB/s       |
| ÷64     | 781 kHz   | 1.28 μs    | 10.24 μs  | 97.6 kB/s      |
| ÷128    | 390 kHz   | 2.56 μs    | 20.48 μs  | 48.8 kB/s      |

### Transfer Timing
- Setup time: 1 system clock cycle (20 ns @ 50 MHz)
- Hold time: 1 system clock cycle (20 ns @ 50 MHz)
- Minimum CS assert time: Transfer time + 2 system clocks

### SD Card Timing Requirements
- **Initialization**: ≤400 kHz (390 kHz ÷128 meets spec)
- **Normal Mode**: ≤25 MHz (25 MHz ÷2 meets spec)
- **High Speed**: Up to 50 MHz (some cards support)

## Resource Usage (Estimated)

Based on similar iCE40 designs:

- **Logic Cells**: ~200 LUTs, ~90 DFFs
- **RAM**: 0 blocks (register-based only)
- **Clock Domain**: Single (50 MHz system clock)

Optimizations for gate efficiency:
- No TX/RX FIFOs (direct register access)
- Power-of-2 clock divider using single counter
- Minimal state machine (3 states)
- 3-bit counter for bit tracking (not 8-bit)
- 7-bit shared divider counter (supports ÷1 to ÷128)

## Limitations

1. **Single byte transfers** - No built-in burst mode
2. **Software CS control** - Manual chip select management
3. **No DMA** - CPU must handle each byte
4. **No interrupts** - Polling only (BUSY/DONE flags)
5. **Fixed 8-bit mode** - No 16/32-bit support

These limitations minimize gate count while maintaining full functionality for typical SPI use cases.

## Design Notes

### Why No FIFO?
- Saves ~256 LUTs and 1 BRAM block
- Most SPI transactions are command-response (few bytes)
- CPU overhead acceptable for 50 MHz system clock

### Why Manual CS?
- Allows multi-slave support with external logic
- No need for CS decoder in peripheral
- Flexibility for complex transactions

### Why Polling Only?
- Simplifies design (no interrupt logic)
- Transfer times are predictable
- CPU can easily poll at 50 MHz

## SD Card Specific Notes

### Initialization Sequence
1. **Power-up delay**: Wait 1 ms after power-up before communication
2. **Clock ≤400 kHz**: Use CLK_DIV=111 (390 kHz) during initialization
3. **74+ dummy clocks**: Send with CS high to allow card to initialize
4. **CMD0**: Send GO_IDLE_STATE command with CRC=0x95
5. **CMD8**: Send SEND_IF_COND to determine SD version (SDv2 vs SDv1)
6. **ACMD41**: Loop until card ready (R1 response = 0x00)
7. **Switch to high speed**: Change to CLK_DIV=001 (25 MHz) for data transfer

### SD Card SPI Mode Requirements
- **Mode**: Mode 0 (CPOL=0, CPHA=0) - Standard for SD cards
- **CS**: Must be controlled per transaction (no multi-byte auto-CS)
- **Response timing**: Wait up to 8 bytes for R1 response
- **Data blocks**: 512 bytes per block (firmware must handle)
- **CRC**: Required during initialization, optional after (CMD59 to enable/disable)

### Recommended SD Card Library
Consider using [FatFs](http://elm-chan.org/fsw/ff/00index_e.html) or similar lightweight library for:
- Block device interface
- FAT filesystem support
- File I/O operations

### Example SD Card Read Sequence
```c
// After initialization complete (ACMD41 done, high speed enabled)
void sd_read_block(uint32_t block_addr, uint8_t *buffer) {
    SPI_CS = 0;

    // Send CMD17 (READ_SINGLE_BLOCK)
    spi_transfer(0x40 | 17);
    spi_transfer((block_addr >> 24) & 0xFF);
    spi_transfer((block_addr >> 16) & 0xFF);
    spi_transfer((block_addr >> 8) & 0xFF);
    spi_transfer(block_addr & 0xFF);
    spi_transfer(0xFF);  // CRC (ignored if disabled)

    // Wait for R1 response (up to 8 bytes)
    uint8_t r1;
    for (int i = 0; i < 8; i++) {
        r1 = spi_transfer(0xFF);
        if (r1 != 0xFF) break;
    }

    // Wait for start token (0xFE)
    uint8_t token;
    while ((token = spi_transfer(0xFF)) != 0xFE);

    // Read 512 bytes of data
    for (int i = 0; i < 512; i++) {
        buffer[i] = spi_transfer(0xFF);
    }

    // Read 2-byte CRC (ignore)
    spi_transfer(0xFF);
    spi_transfer(0xFF);

    SPI_CS = 1;
}
```

### SD Card Performance
At 25 MHz SPI clock:
- **Theoretical**: 3.125 MB/s
- **Actual**: ~2.5 MB/s (accounting for command overhead)
- **512-byte block**: ~200 μs transfer time
- **4 KB transfer**: ~1.6 ms

## Testing Checklist

- [ ] Loopback test (MOSI→MISO) at all clock speeds
- [ ] SPI Mode 0-3 verification
- [ ] CS timing verification (setup/hold)
- [ ] Back-to-back transfers
- [ ] SD card initialization sequence
- [ ] SD card block read/write
- [ ] SD card at 390 kHz init, 25 MHz operation

## Future Enhancements (If Gates Available)

1. **TX FIFO** - Queue multiple bytes (~128 LUTs)
2. **RX FIFO** - Buffer received data (~128 LUTs)
3. **Interrupt support** - Transfer complete IRQ (~20 LUTs)
4. **Hardware CS decoder** - Auto-CS per slave (~50 LUTs)
5. **16-bit mode** - 16-bit transfers (~30 LUTs)

## References

### SPI Standards
- SPI Specification: https://www.nxp.com/docs/en/data-sheet/SPI_BLOCK_GUIDE.pdf
- Motorola SPI Block Guide: https://web.archive.org/web/20150413003534/http://www.ee.nmt.edu/~teare/ee308l/datasheets/S12SPIV3.pdf

### SD Card Resources
- SD Physical Layer Simplified Specification: https://www.sdcard.org/downloads/pls/
- SD Card SPI Mode Tutorial: http://elm-chan.org/docs/mmc/mmc_e.html
- FatFs - Generic FAT Filesystem Module: http://elm-chan.org/fsw/ff/00index_e.html

### Hardware Platform
- PicoRV32 Documentation: https://github.com/YosysHQ/picorv32
- iCE40 FPGA Documentation: https://www.latticesemi.com/iCE40
- Olimex iCE40HX8K-EVB: https://www.olimex.com/Products/FPGA/iCE40/iCE40HX8K-EVB/

## Author

Michael Wolak (mikewolak@gmail.com, mike@epromfoundry.com)

October 2025
