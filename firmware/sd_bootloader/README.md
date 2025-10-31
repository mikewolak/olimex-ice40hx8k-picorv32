# SD Card Bootloader

## Overview

This is an SD card bootloader designed to replace the UART bootloader in the HDL bitstream. It runs from BRAM at address 0x10000 and loads the main bootloader from the SD card into SRAM at address 0x0, then jumps to it.

## Features

- **Compact**: Only ~2.6KB, fits easily in 8KB BRAM
- **Simple**: Sequential execution, no interrupts needed
- **Fast**: Loads 192KB from SD card in chunks with progress indication
- **Verbose**: Uses UART for status output (no data transfer needed)
- **Standalone**: Minimal SD/SPI driver with no external dependencies

## Memory Layout

```
BRAM:  0x10000 - 0x11FFF (8KB)   - Bootloader code
SRAM:  0x00000 - 0x7FFFF (512KB) - Target for loaded bootloader
Stack: 0x80000                   - Grows down from top of SRAM
```

## SD Card Layout

The bootloader reads raw sectors from the SD card:

```
Sector 0:       MBR (Master Boot Record) - not used by bootloader
Sectors 1-375:  Main bootloader code (192KB = 375 sectors × 512 bytes)
Sectors 376+:   Available for other uses
```

## Boot Process

1. Initialize stack pointer
2. Clear BSS section
3. Print boot banner to UART
4. Initialize SD card (SPI mode)
5. Read 375 sectors (192KB) from SD card starting at sector 1
6. Load data to SRAM at address 0x0
7. Show progress via UART (dots and percentages)
8. Jump to address 0x0 to execute loaded bootloader

## Building

```bash
make            # Build all (ELF, BIN, HEX)
make clean      # Remove build artifacts
make disasm     # Create disassembly listing
make help       # Show available targets
```

## Output Files

- `sd_bootloader.elf` - Executable with debug symbols
- `sd_bootloader.bin` - Raw binary for uploading to FPGA
- `sd_bootloader.hex` - Verilog hex format for HDL synthesis
- `sd_bootloader.map` - Linker map file
- `sd_bootloader.asm` - Disassembly listing (created with `make disasm`)

## Integration with HDL

To integrate this bootloader into the FPGA bitstream:

1. Build the bootloader: `make`
2. Use `sd_bootloader.hex` in Verilog synthesis
3. Configure PicoRV32 to start execution at 0x10000
4. Ensure 8KB BRAM is available at 0x10000-0x11FFF

## Preparing SD Card

To prepare an SD card for use with this bootloader:

1. Write the main bootloader to raw sectors 1-375:
   ```bash
   # From sd_card_manager menu:
   # - Upload Bootloader (UART) or
   # - Upload Bootloader (Compressed)
   ```

2. The bootloader partition occupies sectors 1-1024 (512KB total)

## UART Output Example

```
========================================
PicoRV32 SD Card Bootloader v1.0
========================================
Loading bootloader from SD card...

Initializing SD card...
  Status: OK

Reading bootloader from SD card...
  Start sector: 1
  Sector count: 375 (192000 bytes)
  Load address: 0x00000000

Loading to RAM.... 12%.... 25%.... 37%.... 50%.... 62%.... 75%.... 87%.... 100%

========================================
Boot Complete!
========================================
Loaded: 192000 bytes to 0x00000000
Jumping to bootloader...
```

## Error Handling

The bootloader handles errors with LED indicators:

- **Solid LED**: Normal boot in progress
- **Blinking LED**: SD card initialization failed (cannot boot)
- **LED off + hang**: SD card read failed

Error messages are also printed to UART for debugging.

## Files

- `sd_bootloader.c` - Main bootloader code with UART output
- `sd_spi_minimal.c` - Minimal SD/SPI driver (no FatFS dependency)
- `sd_spi_minimal.h` - SD/SPI driver header
- `start_bootloader.S` - Assembly startup code (no interrupts)
- `bootloader.ld` - Linker script (places code at 0x10000)
- `Makefile` - Build system
- `README.md` - This file

## Technical Notes

### Why 375 Sectors?

The bootloader reads 375 sectors (192,000 bytes) to support the maximum size of the main bootloader. This matches the bootloader partition size used in the `sd_card_manager` firmware.

### SPI Initialization

The SD card is initialized in SPI mode at 390 KHz, then switched to 12.5 MHz for data transfers. This ensures compatibility with all SD card types (SDSC, SDHC, SDXC).

### Chunk Reading

Data is read in 64-sector chunks (32KB) with progress indication. This provides visual feedback during boot without significantly impacting performance.

### No Newlib Dependency

The bootloader uses its own minimal UART functions and does not depend on newlib or any other C library. This keeps the code size small and eliminates external dependencies.

## Future Enhancements

Possible future improvements:

- CRC verification of loaded data
- Fallback to UART bootloader if SD fails
- Support for compressed bootloader images
- Boot menu for selecting different bootloaders

## License

Copyright (c) October 2025 Michael Wolak
Email: mikewolak@gmail.com, mike@epromfoundry.com
