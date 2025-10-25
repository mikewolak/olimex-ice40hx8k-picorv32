# SD Card Manager with FatFS

Full-featured SD card file manager for the Olimex iCE40HX8K-EVB RISC-V platform with interactive terminal UI.

## Overview

This application provides a complete SD card interface with FAT32 filesystem support using the FatFS library. It features an ncurses-style terminal interface for browsing files, formatting cards, and managing SD card operations.

**Key Features:**
- SD card detection and initialization
- FAT32 filesystem support (FAT12/16 also supported)
- Card formatting capability
- File browser (to be implemented)
- Adjustable SPI clock speeds (390 kHz - 50 MHz)
- Safe card ejection
- Interactive terminal UI with status bar

**Binary Size:** 75 KB (fits comfortably in 256 KB code space)

## Architecture

### Modular Design

The codebase follows a clean, modular architecture separating hardware abstraction from application logic:

```
firmware/sd_fatfs/
├── hardware.h          - Hardware register definitions (UART, SPI, Timer, LEDs, Buttons)
├── io.c / io.h         - Hardware abstraction layer (I/O functions for all peripherals)
├── sd_spi.c / sd_spi.h - SD card SPI protocol implementation
├── diskio.c            - FatFS disk I/O interface (bridges FatFS ↔ SD card driver)
├── ffconf.h            - FatFS configuration
├── sd_card_manager.c   - Main application with TUI
├── Makefile            - Local build system (downloads FatFS, compiles all sources)
└── fatfs/              - FatFS library (auto-downloaded)
```

### Hardware Abstraction

**hardware.h** - Central hardware register definitions:
- UART peripheral (0x80000000) - Serial communication @ 115200 bps
- Timer peripheral (0x80000020) - 32-bit countdown timer @ 50 MHz
- LED peripheral (0x80000010) - 2 user LEDs
- Button peripheral (0x80000018) - 2 user buttons (future: soft eject)
- SPI Master (0x80000050) - 8 clock speeds, manual CS control

**io.c** - Hardware abstraction functions:
- `uart_putc/getc/puts` - Serial I/O (required by incurses)
- `timer_delay_ms/us` - Precise delays using hardware timer
- `led_on/off/toggle` - Visual indicators
- `button_read/wait_press` - User input (future use)
- `spi_init/transfer/cs_*` - Low-level SPI operations

### SD Card Driver Stack

```
Application (sd_card_manager.c)
    ↓
FatFS Library (ff.c)
    ↓
Disk I/O Interface (diskio.c)
    ↓
SD Card SPI Driver (sd_spi.c)
    ↓
Hardware Abstraction (io.c)
    ↓
Hardware Registers (hardware.h)
```

## Building

### Prerequisites

- RISC-V toolchain (`riscv64-unknown-elf-gcc`)
- Newlib C library (built and installed in `../build/sysroot`)
- `wget` or `curl` (for downloading FatFS)
- `unzip`

### Build Commands

```bash
# From firmware/ directory:
make sd_card_manager

# Or from firmware/sd_fatfs/ directory:
make all
```

The Makefile automatically:
1. Downloads FatFS R0.15 library (if not present)
2. Compiles all source files with correct RISC-V flags
3. Links with newlib, incurses, and FatFS
4. Generates .bin, .elf, .lst, and .map files

### Clean Build

```bash
make clean          # Remove object files
make distclean      # Remove objects + FatFS library
```

## Hardware Configuration

### SPI Connection

The SD card connects via SPI Master peripheral:
- **Clock:** Configurable (390 kHz for init, up to 50 MHz for transfers)
- **Mode:** CPOL=0, CPHA=0 (Mode 0)
- **CS:** Manual control via SPI_CS register

### Pin Mapping (verify with your hardware)

```
SPI_MOSI  → SD Card DI (Data In)
SPI_MISO  ← SD Card DO (Data Out)
SPI_CLK   → SD Card CLK (Clock)
SPI_CS    → SD Card CS (Chip Select)
GND       → SD Card GND + VSS
3.3V      → SD Card VDD (some adapters need 5V for level shifters)
```

**Note:** This adapter has no card detect pin - use Button 0 for soft eject in future.

## FatFS Configuration

Configured in `ffconf.h`:
- **Filesystem:** FAT12/16/32 (exFAT disabled)
- **Long filenames:** Disabled (8.3 format only, saves code space)
- **Code page:** 437 (US English)
- **Read/Write:** Full read-write support
- **Format support:** Enabled (`FF_USE_MKFS = 1`)
- **Volume label:** Enabled (`FF_USE_LABEL = 1`)
- **Timestamp:** Fixed date (2025-01-01) - no RTC

## Usage

### Menu Options

1. **Detect SD Card** - Initialize and mount card
2. **Card Information** - Display CID/CSD registers
3. **Format Card (FAT32)** - Low-level format (erases all data!)
4. **File Browser** - Browse/manage files (to be implemented)
5. **Create Test File** - File I/O testing (to be implemented)
6. **Read/Write Benchmark** - Performance testing (to be implemented)
7. **SPI Speed Configuration** - Adjust clock speed
8. **Eject Card** - Safe unmount

### Navigation

- **Up/Down arrows** or **k/j** - Navigate menu
- **Enter** - Select option
- **q** - Quit application
- **ESC** - Cancel/back (in submenus)

### Status Bar

Bottom of screen shows:
- Card detection status
- Filesystem mount status
- Current SPI clock speed

## Future Enhancements

### Short Term
- [ ] Implement file browser
- [ ] File creation/deletion
- [ ] Read/write benchmarks
- [ ] LED activity indicators during transfers

### Medium Term
- [ ] Button 0 → Soft eject (unmount + LED indicator)
- [ ] Button 1 → Refresh file list
- [ ] CID/CSD register parsing and display
- [ ] Sector count calculation from CSD

### Long Term
- [ ] Multiple SD card support (if hardware permits)
- [ ] File hex editor integration
- [ ] Directory creation/navigation
- [ ] File copy/move operations

## Memory Usage

```
Code (text):  75,860 bytes
Data:            492 bytes
BSS:             944 bytes
Total:        77,296 bytes (75 KB)
```

**Available:**
- Code/Data: 256 KB total
- Heap: 248 KB (for file buffers)
- Stack: Grows down from 0x80000

## Troubleshooting

### Card Not Detected

1. Check SPI wiring and connections
2. Try slower SPI clock (390 kHz or 781 kHz)
3. Verify SD card is inserted correctly
4. Check power supply (3.3V or 5V depending on adapter)

### Mount Failed

1. Card may need formatting (use Format Card option)
2. Check filesystem type (only FAT12/16/32 supported)
3. Some cards require specific initialization sequences

### Build Errors

1. Ensure newlib is built and installed
2. Check that FatFS downloaded correctly (`sd_fatfs/fatfs/`)
3. Verify RISC-V toolchain is in PATH
4. Try `make distclean && make`

## References

- **FatFS:** http://elm-chan.org/fsw/ff/00index_e.html
- **SD Card Spec:** https://www.sdcard.org/downloads/pls/
- **SPI Mode:** Simplified SD card protocol (vs. native 4-bit mode)

## License

Copyright (c) October 2025 Michael Wolak (mikewolak@gmail.com)

This is a reference implementation for educational purposes.
