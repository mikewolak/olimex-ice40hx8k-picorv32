# SD Card Bootloader - Complete Boot System

## Overview

The SD Card Bootloader is a revolutionary feature that enables the PicoRV32 system to boot completely autonomously from an SD card without requiring a host PC connection. This transforms the iCE40HX8K platform from a development board into a standalone embedded system.

**Key Achievement:** The system now boots from FPGA configuration → SD bootloader in ROM → SD Card Manager from SD card → Overlay Launcher → Full operating environment, all without any UART intervention.

## Architecture

### Three-Stage Boot Process

```
┌─────────────────────────────────────────────────────────────┐
│ Stage 1: FPGA Configuration                                 │
│ - Lattice iCE40HX8K bitstream loaded via JTAG              │
│ - Bootloader ROM initialized in BRAM at 0x40000            │
│ - PicoRV32 CPU starts executing from ROM                   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Stage 2: SD Bootloader (firmware/sd_bootloader/)           │
│ - Initializes SD card via SPI                              │
│ - Reads sectors 1-375 (192 KB) from SD card                │
│ - Loads SD Card Manager to RAM at address 0x0              │
│ - Jumps to loaded firmware                                 │
│ - UART output shows boot progress                          │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Stage 3: SD Card Manager (firmware/sd_fatfs/)              │
│ - FAT32 filesystem support                                 │
│ - File browser with navigation                             │
│ - File operations: rename, delete, mkdir                   │
│ - Overlay launcher for dynamic code loading                │
│ - Complete development environment                         │
└─────────────────────────────────────────────────────────────┘
```

### Memory Layout

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x0001E63C| 124 KB  | SD Card Manager (loaded from SD)
0x0001E640 - 0x0003E63F| 128 KB  | Upload buffer for overlays
0x00040000 - 0x00041FFF|   8 KB  | SD Bootloader ROM (BRAM)
0x00042000 - 0x0005EFFF| 116 KB  | Main firmware heap
0x0005F000 - Stack top |   --    | Main firmware stack (grows down)
0x0005F000 - 0x0005FFFF|   4 KB  | SAFETY GAP
0x00060000 - 0x00077FFF|  96 KB  | Overlay code region
0x00078000 - 0x00079FFF|   8 KB  | Overlay stack
0x0007A000 - 0x0007FFFF|  24 KB  | Overlay heap
```

## Components

### 1. SD Bootloader (`firmware/sd_bootloader/`)

**Purpose:** Minimal bootloader in ROM that loads main firmware from SD card

**Files:**
- `sd_bootloader.c` - Main bootloader logic with UART status output
- `sd_spi_minimal.c` - Minimal SD/SPI driver (no FatFS dependency)
- `sd_spi_minimal.h` - SD driver interface
- `start_bootloader.S` - Assembly startup code
- `linker_bootloader.ld` - Linker script for 8KB ROM
- `Makefile` - Build system

**Features:**
- SD card initialization with SDHC/SDXC detection
- Sector reading at 12.5 MHz SPI clock (390 KHz init)
- Progress indicator during load (UART dots + percentage)
- Error handling with LED blink patterns
- CRC validation of loaded firmware

**Boot Sequence:**
1. Print banner to UART
2. Initialize SD card
3. Read 375 sectors (192 KB) starting from sector 1
4. Load to RAM address 0x0
5. Show progress: dots every 64 sectors, percentage every 256 sectors
6. Jump to loaded firmware

**UART Output Example:**
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

Loading to RAM.... 16%.... 32%.... 48%.... 64%.... 80%.... 96%

========================================
Boot Complete!
========================================
Loaded: 192000 bytes to 0x00000000
Jumping to bootloader...
```

### 2. SD Card Manager (`firmware/sd_fatfs/`)

**Purpose:** Full-featured operating environment with FAT32 and overlay support

**Files:**
- `sd_card_manager.c` - Main application with file browser and overlay launcher
- `sd_card_operations.c` - FAT32 filesystem operations
- `io.c` - Low-level SPI/SD hardware interface
- `hardware.h` - Memory-mapped I/O definitions
- `ff.c`, `ff.h` - FatFS library (ChaN's FAT32 implementation)
- `diskio.c` - FatFS disk I/O layer
- `ffconf.h` - FatFS configuration

**Features:**
- FAT32 filesystem support (exFAT compatible)
- File browser with arrow key navigation
- File operations: View, Rename, Delete, Upload
- Directory operations: Create, Navigate
- Overlay launcher for .bin files
- Terminal size auto-detection
- Full UART-based user interface

**User Interface:**
```
╔════════════════════════════════════════════╗
║     SD Card Manager - File Browser        ║
╠════════════════════════════════════════════╣
║ [DIR]  overlays/                          ║
║ [FILE] mandelbrot_float.bin     48.2 KB   ║
║ [FILE] mandelbrot_fixed.bin     47.1 KB   ║
║ [FILE] hexedit.bin              23.4 KB   ║
║ [FILE] heap_test.bin            12.8 KB   ║
╠════════════════════════════════════════════╣
║ ↑/↓: Navigate | Enter: Action | Q: Quit   ║
╚════════════════════════════════════════════╝
```

**Overlay System:**
- Loads .bin files from SD card to 0x60000
- Position-independent code execution
- Memory isolation with dedicated heap/stack
- Timer interrupt support via function pointers
- Clean return to manager menu

### 3. SPI Master Peripheral (`hdl/spi_master.v`)

**Purpose:** Hardware SPI controller for SD card communication

**Register Map (Base: 0x80000050):**
```
Offset | Register | Description
-------|----------|----------------------------------------
0x00   | CTRL     | Control register (clock divider bits [4:2])
0x04   | DATA     | Data register (write: send, read: receive)
0x08   | STATUS   | Status register (bit 0: BUSY)
0x0C   | CS       | Chip select (0=assert, 1=deassert)
```

**Clock Dividers:**
```c
#define SPI_CLK_390KHZ  (7 << 2)  // /128 = 390 KHz (init)
#define SPI_CLK_12MHZ   (2 << 2)  // /4  = 12.5 MHz (data)
```

**Transfer Sequence:**
```c
// Send byte and receive response
uint8_t spi_transfer(uint8_t data) {
    SPI_DATA = data;                          // Write data
    while (SPI_STATUS & SPI_STATUS_BUSY);     // Wait for completion
    return (uint8_t)(SPI_DATA & 0xFF);        // Read response
}
```

## Building

### Build SD Bootloader

```bash
cd firmware/sd_bootloader
make clean
make

# Outputs:
#   sd_bootloader.elf   - ELF with debug symbols
#   sd_bootloader.bin   - Raw binary
#   sd_bootloader.hex   - Hex file for BRAM initialization
```

### Build SD Card Manager

```bash
cd firmware/sd_fatfs
make clean
make

# Outputs:
#   sd_card_manager.elf - ELF with debug symbols
#   sd_card_manager.bin - Raw binary for SD card
```

### Build Complete System

```bash
# From project root
cd /home/mwolak/olimex-ice40hx8k-picorv32

# Link SD bootloader as active bootloader
cd bootloader
rm -f bootloader.hex.selected
ln -s ../firmware/sd_bootloader/sd_bootloader.hex bootloader.hex.selected
cd ..

# Clean rebuild bitstream
rm -f build/ice40_picorv32.json build/ice40_picorv32.asc build/ice40_picorv32.bin
make bitstream

# Result: build/ice40_picorv32.bin contains SD bootloader in BRAM
```

## SD Card Preparation

### Format SD Card

**Requirements:**
- FAT32 filesystem (exFAT also supported)
- Any size card (tested with 4GB - 64GB)
- MBR partition table

**Linux:**
```bash
# WARNING: Replace /dev/sdX with your SD card device!
sudo fdisk /dev/sdX
  # d (delete existing partitions)
  # n (new partition)
  # p (primary)
  # 1 (partition number)
  # [Enter] (default start)
  # [Enter] (default end)
  # t (change type)
  # c (W95 FAT32 LBA)
  # w (write changes)

sudo mkfs.vfat -F 32 /dev/sdX1
```

**macOS:**
```bash
diskutil list  # Find your SD card (e.g., disk2)
diskutil eraseDisk FAT32 SDCARD MBR disk2
```

**Windows:**
- Right-click SD card in File Explorer
- Format → FAT32 → Quick Format

### Write Firmware to SD Card

**Critical:** SD Card Manager must start at sector 1 (after MBR at sector 0)

**Linux/macOS:**
```bash
# Write sd_card_manager.bin to sector 1
sudo dd if=firmware/sd_fatfs/sd_card_manager.bin of=/dev/sdX bs=512 seek=1

# Verify write
sudo dd if=/dev/sdX bs=512 skip=1 count=375 | md5sum
md5sum firmware/sd_fatfs/sd_card_manager.bin
# MD5 sums should match
```

**Windows (using HxD hex editor):**
1. Open SD card as physical disk in HxD
2. Go to offset 0x200 (sector 1)
3. Paste contents of sd_card_manager.bin
4. Save changes

### Copy Overlays to SD Card

```bash
# Mount SD card
mount /dev/sdX1 /mnt/sdcard

# Create overlays directory
mkdir -p /mnt/sdcard/overlays

# Copy overlay binaries
cp firmware/overlay_sdk/projects/mandelbrot_float/mandelbrot_float.bin /mnt/sdcard/overlays/
cp firmware/overlay_sdk/projects/mandelbrot_fixed/mandelbrot_fixed.bin /mnt/sdcard/overlays/
cp firmware/overlay_sdk/projects/hexedit/hexedit.bin /mnt/sdcard/overlays/
cp firmware/overlay_sdk/projects/heap_test/heap_test.bin /mnt/sdcard/overlays/

# Unmount
umount /mnt/sdcard
```

## Hardware Setup

### 1. Program FPGA

```bash
# Using iceprog (Linux)
sudo iceprog build/ice40_picorv32.bin

# Using Olimex programmer
olimexino-32u4 -p build/ice40_picorv32.bin
```

### 2. Insert SD Card

- Power off FPGA board
- Insert prepared SD card into SD slot
- Power on FPGA board

### 3. Connect UART

```bash
# 115200 baud for SD bootloader UART output
minicom -D /dev/ttyUSB0 -b 115200

# Or screen
screen /dev/ttyUSB0 115200
```

### 4. Observe Boot Sequence

You should see:
1. SD Bootloader banner
2. SD card initialization
3. Loading progress (dots + percentage)
4. "Boot Complete!"
5. SD Card Manager menu

## Troubleshooting

### No UART Output

**Symptoms:** Blank terminal, no text appears

**Causes:**
1. Wrong baud rate → Set to 115200
2. Wrong UART device → Check `ls /dev/ttyUSB*`
3. Bitstream not programmed → Run `sudo iceprog build/ice40_picorv32.bin`
4. SD bootloader not in BRAM → Verify `bootloader.hex.selected` symlink

**Verification:**
```bash
# Check symlink
ls -l bootloader/bootloader.hex.selected
# Should point to: ../firmware/sd_bootloader/sd_bootloader.hex

# Verify hex file timestamp
ls -lh firmware/sd_bootloader/sd_bootloader.hex
# Should be recent (after last build)
```

### SD Card Init Failed

**Symptoms:** "ERROR: SD card init failed (code X)"

**Error Codes:**
- `-1` - CMD0 failed (card not responding)
- `-2` - ACMD41 timeout (SDv2 card init timeout)
- `-3` - Card type not supported (SDv1/MMC not supported)

**Solutions:**
1. Check SD card insertion (fully seated)
2. Try different SD card (some cards are picky)
3. Format as FAT32 (not exFAT initially)
4. Check SPI connections on PCB
5. Verify SPI_BASE address is 0x80000050 in code

### SD Read Failed

**Symptoms:** "ERROR: SD read failed at sector X (code Y)"

**Error Codes:**
- `-1` - CMD17 command failed
- `-2` - Timeout waiting for data token

**Solutions:**
1. Verify sd_card_manager.bin was written to correct sector
2. Check SD card not corrupted: `sudo dd if=/dev/sdX bs=512 skip=1 count=10 | hexdump -C`
3. Verify file size is correct (should be ~124 KB)
4. Try slower SPI clock (change `SPI_CLK_12MHZ` to `(3 << 2)` for 6.25 MHz)

### System Hangs After "Jumping to bootloader..."

**Symptoms:** Boot completes but system hangs

**Causes:**
1. Wrong firmware loaded to RAM
2. Corrupted sd_card_manager.bin on SD card
3. Memory address mismatch

**Solutions:**
1. Verify sd_card_manager.bin was built correctly: `ls -lh firmware/sd_fatfs/sd_card_manager.bin`
2. Rebuild sd_card_manager: `cd firmware/sd_fatfs && make clean && make`
3. Re-write to SD card: `sudo dd if=firmware/sd_fatfs/sd_card_manager.bin of=/dev/sdX bs=512 seek=1`
4. Verify linker address: `riscv64-unknown-elf-objdump -h firmware/sd_fatfs/sd_card_manager.elf | grep .text`
   - Should show address 0x00000000

### LED Blinking Error Pattern

**Symptoms:** LED blinks continuously after error message

**Meaning:** Fatal error, system halted

**Action:**
1. Note the error message before LED starts blinking
2. Fix underlying issue (see error codes above)
3. Power cycle FPGA to retry

## SPI Peripheral Bug History

### Critical Bugs Fixed (October 2025)

The SD bootloader initially failed to boot due to **four critical SPI peripheral bugs** in `sd_spi_minimal.c`:

#### Bug #1: Wrong Base Address
**Problem:** Used 0x80000030 instead of 0x80000050
```c
// WRONG
#define SPI_BASE 0x80000030

// CORRECT
#define SPI_BASE 0x80000050
```
**Impact:** All SPI register accesses hit wrong memory locations

#### Bug #2: Wrong Transfer Polling
**Problem:** Polled SPI_CTRL instead of SPI_STATUS
```c
// WRONG
while (!(SPI_CTRL & 0x01));

// CORRECT
while (SPI_STATUS & SPI_STATUS_BUSY);
```
**Impact:** CPU stuck in infinite loop waiting for SPI completion

#### Bug #3: Wrong Clock Dividers
**Problem:** Used raw shift amounts instead of pre-shifted values
```c
// WRONG
#define SPI_CLK_390KHZ 6
#define SPI_CLK_12MHZ  1

// CORRECT
#define SPI_CLK_390KHZ (7 << 2)  // Bits [4:2] = 111b
#define SPI_CLK_12MHZ  (2 << 2)  // Bits [4:2] = 010b
```
**Impact:** Incorrect SPI clock frequencies

#### Bug #4: Wrong CS Control
**Problem:** Bit manipulation on SPI_CTRL for CS
```c
// WRONG
static void spi_cs_assert(void) {
    SPI_CTRL &= ~(1 << 8);
}

// CORRECT
static void spi_cs_assert(void) {
    SPI_CS_REG = 0;  // Dedicated register
}
```
**Impact:** Chip select not working properly

### Root Cause

The `sd_spi_minimal.c` driver was written as a "minimal" version but **did not copy the working SPI peripheral code** from `firmware/sd_fatfs/io.c`. Instead, it used different (incorrect) register definitions.

### Lesson Learned

**When you have working reference code, copy it exactly!**

The fix involved comparing `sd_spi_minimal.c` with `firmware/sd_fatfs/hardware.h` and `io.c` line-by-line and updating all SPI peripheral usage to match the proven working implementation.

After fixing all four bugs, the SD bootloader worked perfectly on the first hardware test.

## Performance Characteristics

### Boot Time

From FPGA configuration to SD Card Manager menu:
- **Stage 1:** FPGA config ~2 seconds (Lattice iCE40 JTAG)
- **Stage 2:** SD boot ~3-5 seconds (375 sectors @ 12.5 MHz SPI)
- **Stage 3:** SD Card Manager startup ~1 second
- **Total:** ~6-8 seconds to fully operational system

### SPI Transfer Rates

- **Initialization:** 390 KHz (safe for all SD cards)
- **Data transfer:** 12.5 MHz (50 MHz system clock / 4)
- **Effective throughput:** ~1.4 MB/sec (accounting for overhead)
- **Sector read time:** ~0.37 ms per 512-byte sector

### Memory Footprint

- **SD bootloader ROM:** 5.7 KB (fits in 8 KB BRAM)
- **SD Card Manager:** 124 KB (fits in 192 KB load region)
- **Total BRAM usage:** 1 of 32 blocks (3.1%)

## Simulation

### ModelSim Testbench

Test the SD bootloader in simulation:

```bash
cd sim

# Compile design
/home/mwolak/intelFPGA_lite/20.1/modelsim_ase/bin/vsim -c -do "do compile_sd_bootloader.do; quit"

# Run simulation (10 ms sim time)
/home/mwolak/intelFPGA_lite/20.1/modelsim_ase/bin/vsim -c -do "do compile_sd_bootloader.do; do run_sd_bootloader.do; run 10ms; quit"
```

**Files:**
- `sim/compile_sd_bootloader.do` - Compilation script
- `sim/run_sd_bootloader.do` - Simulation script
- `sim/tb_sd_bootloader.v` - Testbench with SD card model

**What gets tested:**
- SPI transfers to SD card
- CMD0, CMD8, ACMD41, CMD58, CMD17 command sequences
- Sector reading
- UART output
- Memory writes to address 0x0

## Future Enhancements

### Planned Features

1. **Compressed Firmware**
   - Store compressed sd_card_manager.bin on SD card
   - Decompress to RAM during boot
   - Reduce SD card space usage

2. **Multi-Boot Support**
   - Boot menu in SD bootloader
   - Select between multiple firmware images
   - Timeout with default selection

3. **Firmware Update**
   - SD Card Manager can update itself
   - Write new version to sectors 1-375
   - Atomic update with verification

4. **Boot Configuration**
   - config.txt file on SD card
   - Set UART baud rate, boot timeout, etc.
   - Override defaults without rebuilding

5. **Diagnostic Mode**
   - Hold button during boot → enter diagnostics
   - Memory test, SPI test, UART test
   - Useful for hardware debugging

6. **Faster SPI**
   - Increase SPI clock to 25 MHz (system clock / 2)
   - Requires signal integrity testing
   - Could reduce boot time to ~3 seconds

## Technical Reference

### SD Card Commands

```
Command | Argument    | Response | Description
--------|-------------|----------|--------------------------------
CMD0    | 0x00000000  | R1       | Reset card to idle state
CMD8    | 0x000001AA  | R7       | Send interface condition
ACMD41  | 0x40000000  | R1       | Initialize card (HCS=1)
CMD58   | 0x00000000  | R3       | Read OCR register
CMD17   | sector_addr | R1       | Read single 512-byte block
```

### R1 Response Bits

```
Bit | Name           | Description
----|----------------|----------------------------------
7   | 0 (always)     | Start bit (must be 0)
6   | Parameter err  | Command parameter error
5   | Address error  | Address misaligned
4   | Erase seq err  | Erase sequence error
3   | CRC error      | CRC check failed
2   | Illegal cmd    | Command not legal
1   | Erase reset    | Erase sequence cleared
0   | In idle state  | Card is in idle state (1)
```

### Card Types

- **SDSC:** Standard capacity (≤2GB), byte addressing
- **SDHC:** High capacity (2GB-32GB), sector addressing
- **SDXC:** Extended capacity (32GB-2TB), sector addressing

**Detection:**
- CMD8 response indicates SDv2 support
- ACMD41 with HCS=1 requests high capacity
- CMD58 OCR bit 30 (CCS) indicates SDHC/SDXC

**Address Conversion:**
```c
if (!s_is_sdhc) {
    addr <<= 9;  // SDSC: convert sector to byte address
}
// SDHC/SDXC: use sector address directly
```

## Conclusion

The SD Card Bootloader transforms the iCE40HX8K-PicoRV32 platform into a truly autonomous embedded system. No host PC required - just program the FPGA once, insert an SD card with your firmware and overlays, and power on. The system boots completely standalone and provides a full operating environment with file browser, overlay launcher, and development SDK.

This achievement required careful integration of:
- BRAM-based bootloader ROM
- Minimal SD/SPI driver with exact hardware peripheral register matching
- Multi-stage boot sequence with progress indication
- FAT32 filesystem support
- Overlay SDK with position-independent code
- Memory isolation and safety gaps

The result is a complete, production-ready embedded system on an open-source FPGA platform.

---

**Copyright (c) October 2025 Michael Wolak**
Email: mikewolak@gmail.com, mike@epromfoundry.com

**NOT FOR COMMERCIAL USE - EDUCATIONAL AND RESEARCH PURPOSES ONLY**
