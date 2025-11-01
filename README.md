# Olimex iCE40HX8K PicoRV32 RISC-V System

**A complete autonomous RISC-V soft-core processor system with SD card boot** for the Olimex iCE40HX8K-EVB FPGA board featuring the PicoRV32 CPU, SD card bootloader, comprehensive peripheral set, and complete development environment with overlay SDK.

## NOT FOR COMMERCIAL USE
**EDUCATIONAL AND RESEARCH PURPOSES ONLY**

This project is provided for educational and research purposes. Commercial use is strictly prohibited without explicit written permission.

Copyright (c) October 2025 Michael Wolak
Email: mikewolak@gmail.com, mike@epromfoundry.com

---

## Overview

This project implements a fully functional RISC-V RV32IM processor system on the Lattice iCE40HX8K FPGA. **Version 1.0 introduces autonomous SD card boot**, transforming the platform from a development board into a complete standalone embedded system.

### Key Features

- **SD Card Autonomous Boot** - System boots completely standalone from SD card, no PC required
- **PicoRV32 CPU Core** - 32-bit RISC-V processor running at 50 MHz
- **512KB External SRAM** - High-performance 5-cycle SRAM controller
- **SD Card Manager** - Complete operating environment with FAT32 file browser and overlay launcher
- **Dynamic Overlay System** - Load and execute firmware from SD card on-the-fly
- **MMIO Peripherals** - UART, SPI, Timer, GPIO, CRC32, and more
- **FreeRTOS RTOS** - Full preemptive multitasking with custom PicoRV32 port
- **Rich Firmware Library** - Over 20 example applications including FreeRTOS demos
- **Professional Build System** - Automatic toolchain management and reproducible builds

## SD Card Autonomous Boot System

**NEW IN VERSION 1.0** - The system now boots completely autonomously from SD card without requiring any PC connection or UART interaction!

### Boot Sequence

1. **FPGA Configuration** - Program bitstream once via JTAG
2. **SD Bootloader (ROM)** - 8KB bootloader in BRAM initializes SD card and loads main firmware
3. **SD Card Manager** - 124KB application boots from SD card with FAT32 file browser
4. **Overlay Launcher** - Execute firmware applications (.bin files) from SD card dynamically

**Total boot time:** 6-8 seconds from power-on to fully operational system

### SD Card Manager Features

The SD Card Manager is a complete operating environment that runs from SD card:

- **FAT32/exFAT Filesystem Support** - Full read/write access to files and directories
- **Built-in SD Card Formatting** - Format and partition SD cards directly from the FPGA
  - Create FAT32 partitions with MBR
  - No PC required for SD card preparation
  - Automatic filesystem initialization
- **File Browser** - Navigate directories with arrow keys, visual file listing
- **File Operations**:
  - View file contents
  - Rename files and directories
  - Delete files
  - Upload new files via UART
  - Create new directories
- **Overlay System** - Load and execute .bin files from SD card
  - Position-independent code (PIC) execution
  - 96KB code region at 0x60000
  - Dedicated heap (24KB) and stack (8KB)
  - Clean return to manager menu
- **Terminal Auto-Detection** - Automatically adapts to terminal size (80x24, 120x40, etc.)

### Quick Start - SD Card Boot

```bash
# 1. Build and program FPGA bitstream (one-time)
make bitstream
sudo iceprog build/ice40_picorv32.bin

# 2. Insert blank SD card and connect UART
minicom -D /dev/ttyUSB0 -b 115200

# 3. System boots and presents SD Card Manager menu
#    Use the built-in formatter to prepare the SD card!
#    - Select "Format SD Card" from menu
#    - Choose FAT32 filesystem
#    - Confirm formatting
#    - SD card is now ready with proper partitions

# 4. Upload overlays and firmware from the SD Card Manager UI
#    - No need to remove the SD card
#    - Upload .bin files directly via UART
#    - Files appear immediately in the file browser

# 5. Launch overlays by selecting them in the file browser
```

### What You Get

- **No PC Dependency** - After initial FPGA programming, system is completely standalone
- **Persistent Storage** - All firmware and data stored on removable SD card
- **Dynamic Code Loading** - Load and execute applications without reprogramming FPGA
- **Complete SDK** - Overlay development kit with newlib, incurses, lwIP libraries
- **File Management** - Full filesystem operations from embedded system
- **Development Environment** - Edit-compile-upload-test cycle entirely through SD card

See [SD_BOOTLOADER.md](SD_BOOTLOADER.md) for complete documentation including:
- Detailed boot sequence explanation
- SD card preparation (with and without formatting tool)
- SPI peripheral specifications
- Troubleshooting guide

See [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) for memory subsystem deep dive including:
- Complete memory hierarchy and address decoding
- SRAM controller timing analysis with waveform diagrams
- Instruction cycle counts and performance metrics
- Best/worst case timing for all access patterns
- Estimated MIPS and benchmark performance
- Optimization strategies

## Key Features

### Hardware Architecture
- **FPGA**: Lattice iCE40HX8K-CT256 (7680 LUTs, 90% utilization)
- **CPU**: PicoRV32 RV32IM @ 50 MHz (multiply/divide, no compressed instructions)
- **Memory**: 512KB external SRAM (K6R4016V1D-TC10)
- **Clock**: 100 MHz crystal, divided to 50 MHz system clock
- **Timing**: Meets 50 MHz requirement with 27.9% margin (63.95 MHz achieved)
- **SPI Master**: Hardware SPI controller for SD card (12.5 MHz data transfer)

### Peripherals (MMIO)
- **UART**: 115200 baud, 8N1, with 64-byte circular buffers
- **Timer**: 32-bit timer with millisecond resolution
- **GPIO**: Configurable I/O pins
- **SRAM Controller**: Optimized 5-cycle access pattern
- **CRC32**: Hardware CRC32 for firmware verification

### Bootloader
- Interactive command-line interface over UART
- Firmware upload with CRC32 verification
- Memory inspection and modification
- Jump to uploaded firmware
- Safe fallback on upload errors

### Minicom-FPGA: Ultra-Fast Firmware Upload
**NEW in Release 0.11** - Custom minicom build with integrated FAST streaming protocol:

- **90-104 KB/sec upload speed** (vs 10 KB/sec with standard protocol)
- **Built-in protocol** - No external upload tools needed
- **Streamlined UX** - Press Ctrl-A + S, select file, done!
- **Only 3 ACKs total** for entire transfer (vs 4096+ in chunked protocol)
- **1 Mbaud UART** - 8.7x faster than standard 115200 baud
- **Direct integration** - FAST protocol implemented in C, runs in minicom process
- **Cross-platform** - Works on Linux and macOS

**Quick Start:**
```bash
# Build Minicom-FPGA
cd tools/minicom-picorv32
./build.sh

# Connect to FPGA
src/minicom -D /dev/ttyUSB0 -b 1000000

# Upload firmware: Press Ctrl-A, then S
# Select file from browser - upload happens automatically!
```

**Performance:** 256KB firmware uploads in ~2.3 seconds (vs ~20 seconds with standard bootloader)

See [Minicom-FPGA Documentation](#minicom-fpga-detailed-guide) for full details.

### Build System
- **Automatic toolchain management** - Downloads and builds required tools
- **Platform verification** - Ensures correct build environment (x86-64 Linux)
- **Reproducible builds** - Version-pinned dependencies
- **Comprehensive reports** - Gate utilization, timing, tool versions
- **Artifact packaging** - Creates release tarballs with git tags

### SD Card Dynamic Overlay System

**NEW** - Advanced runtime code loading system with SD card integration:

The SD Card Manager provides dynamic overlay loading capabilities, allowing firmware applications to be loaded from SD card and executed on demand without reprogramming the FPGA or main firmware.

#### Key Features

- **Dynamic Loading**: Load and execute firmware overlays from FAT32 SD card
- **Position-Independent Code (PIC)**: Overlays compiled with -fPIC for relocatable execution
- **Memory Isolation**: Dedicated overlay memory region with heap and stack separation
- **Clean Return**: Overlays return cleanly to SD Card Manager menu
- **Timer Interrupt Support**: Overlays can register timer handlers via function pointers
- **Full C Library**: Overlays have access to newlib with floating-point printf support

#### Memory Architecture

The system uses a carefully designed memory layout to prevent conflicts between main firmware and overlays:

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x0001E63C| 124 KB  | Main firmware (SD Card Manager)
0x0001E640 - 0x0003E63F| 128 KB  | Upload buffer (temporary)
0x00040000 - 0x00041FFF|   8 KB  | Bootloader (BRAM/ROM)
0x00042000 - 0x0005EFFF| 116 KB  | Main firmware heap
0x0005F000 - Stack top (grows down from 0x5F000)
0x0005F000 - 0x0005FFFF|   4 KB  | SAFETY GAP (prevents corruption)
0x00060000 - 0x00077FFF|  96 KB  | Overlay code/data/bss
0x00078000 - 0x00079FFF|   8 KB  | Overlay stack (grows down)
0x0007A000 - 0x0007FFFF|  24 KB  | Overlay heap (grows up)
```

**Critical Design Point:** The 4KB safety gap (0x5F000-0x60000) prevents main firmware stack overflow from corrupting overlay code.

#### Overlay Memory Regions

1. **Code/Data/BSS** (0x60000-0x78000, 96KB)
   - Position-independent code linked at 0x60000
   - Compiled with `-fPIC -fno-plt` for relocatability
   - Maximum overlay size: 96KB (enforced by linker)

2. **Stack** (0x78000-0x7A000, 8KB)
   - Grows downward from 0x7A000
   - Separate from main firmware stack
   - Overflow protection via linker checks

3. **Heap** (0x7A000-0x80000, 24KB)
   - Grows upward from 0x7A000
   - Used by malloc/newlib for dynamic allocation
   - Sufficient for most overlay applications

#### Interrupt Strategy

Overlays cannot override the firmware's IRQ vector (fixed at address 0x10), so a function pointer approach is used:

1. **Firmware IRQ Handler** (sd_card_manager.c):
   - Maintains global function pointer `overlay_timer_irq_handler` at address 0x1f0e8
   - Checks pointer in timer interrupt and calls if non-NULL

2. **Overlay Registration** (mandelbrot_fixed/float example):
   ```c
   // At startup - register timer handler
   volatile void (**overlay_timer_irq_handler_ptr)(void) = (void (**)(void))0x1f0e8;
   *overlay_timer_irq_handler_ptr = timer_ms_irq_handler;

   // At exit - unregister to prevent crashes
   *overlay_timer_irq_handler_ptr = 0;
   ```

3. **Timer Library** (timer_ms.c):
   - Provides millisecond-accurate timing via timer interrupts
   - Functions: `timer_ms_init()`, `get_millis()`, `sleep_milli()`
   - Marked with `__attribute__((used))` to prevent linker stripping

#### Building Overlays

The overlay SDK provides a complete build environment:

```bash
# Create new overlay project
cd firmware/overlay_sdk
./create_project.sh my_overlay

# Build overlay
cd projects/my_overlay
make all

# Outputs:
#   my_overlay.bin  - Binary for SD card upload
#   my_overlay.elf  - Debug symbols
#   my_overlay.map  - Linker map
```

#### Overlay Makefile Template

Each overlay includes a standardized Makefile:

```makefile
PROJECT_NAME = my_overlay
include ../../Makefile.overlay

SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

# Add libraries (incurses, lwIP, etc.)
OVERLAY_LIBS += -lincurses

# Force floating-point printf support
OVERLAY_LDFLAGS += -Wl,-u,_printf_float

all: $(PROJECT_NAME).bin size
```

#### Example Overlays

**Mandelbrot Performance Tests:**
- `mandelbrot_fixed.bin` (47KB) - Fixed-point arithmetic version
- `mandelbrot_float.bin` (48KB) - Floating-point arithmetic version
- Both auto-start, detect terminal size, display real-time performance metrics
- Interactive controls: R (reset), +/- (iterations), Q (quit)

**Hexedit Visual Editor:**
- `hexedit.bin` (23KB) - Full-screen hex editor with curses interface
- Direct memory editing at overlay address space
- Clean integration with SD Card Manager

**Heap Test:**
- `heap_test.bin` - Dynamic memory allocation verification
- Allocates 32KB blocks, performs CRC32 validation
- Tests overlay heap functionality

#### Technical Details

**Position-Independent Code (PIC):**
- Compiled with `-fPIC` for relocatable execution
- Uses PC-relative addressing for global data access
- GOT (Global Offset Table) at beginning of data section
- No hard-coded absolute addresses

**Linker Configuration:**
- Custom linker script: `overlay_linker.ld`
- Entry point at 0x60000
- Sections: `.text`, `.data`, `.bss`, `.got`
- Stack pointer initialized to 0x7A000 by `overlay_start.S`

**Startup Code (overlay_start.S):**
1. Save caller's SP and RA to fixed location (0x7FC00)
2. Set overlay stack pointer to 0x7A000
3. Clear BSS section
4. Initialize GOT pointer
5. Call main(argc=0, argv=NULL)
6. Call exit() to cleanup newlib
7. Restore caller's SP and RA
8. Return to SD Card Manager

**C Library Support:**
- Full newlib with `-Wl,-u,_printf_float` for floating-point printf
- Syscalls implemented: `_write`, `_read`, `_sbrk`, `_close`, `_fstat`, `_isatty`
- UART-based stdin/stdout/stderr
- Heap management via `_sbrk()` using overlay heap region

**Watchdog Protection:**
- Firmware sets timer in one-shot mode before calling overlay
- If overlay hangs, timer fires and firmware displays LED error pattern
- Prevents system lockup from buggy overlays

#### Performance Considerations

**Overhead:**
- PIC code has ~5-10% performance overhead vs absolute addressing
- Function calls through PLT (Procedure Linkage Table) add indirection
- GOT access for global variables requires extra load

**Binary Size:**
- Floating-point printf adds ~23KB to binary
- Incurses library adds ~5KB
- Newlib base adds ~15KB

**Benchmarks** (Mandelbrot 150x40 @ 256 iterations):
- Fixed-point: ~0.6-0.7 M iter/sec (optimized integer math)
- Floating-point: ~0.03-0.05 M iter/sec (software FP emulation)
- Demonstrates 10-20x performance difference on RV32IM without hardware FP

#### Limitations

1. **No Overlay Chaining**: Overlays cannot load other overlays
2. **No IRQ Vector Override**: Must use function pointer approach for interrupts
3. **Fixed Memory Layout**: Overlay region is hard-coded at 0x60000
4. **96KB Code Limit**: Maximum overlay size enforced by linker
5. **Single Overlay**: Only one overlay runs at a time

#### Future Enhancements

- Multiple overlay slots for quick switching
- Overlay symbol resolution for inter-overlay calls
- Compressed overlays with runtime decompression
- Overlay caching to reduce SD card reads
- Dynamic memory region negotiation

## Quick Start

### Prerequisites

The build system requires **x86-64 Linux**. Ubuntu, Debian, Fedora, RHEL, Arch, and other distributions are supported.

Run platform verification to check your system:
```bash
bash scripts/verify-platform.sh
```

If build tools are missing, the script will provide distribution-specific installation commands.

**Minimum requirements:**
- gcc, g++, make
- git
- wget or curl
- tar

**All other dependencies** (FPGA tools, RISC-V toolchain) are automatically downloaded and built by the Makefile.

### Building

```bash
# Clone the repository
git clone https://github.com/mikewolak/olimex-ice40hx8k-picorv32.git
cd olimex-ice40hx8k-picorv32

# Build everything (toolchain, gateware, firmware, host tools)
make

# This will:
# 1. Verify platform requirements
# 2. Download oss-cad-suite (Yosys, NextPNR, icetime)
# 3. Download and build RISC-V GCC toolchain
# 4. Build bootloader
# 5. Synthesize FPGA bitstream
# 6. Build all firmware examples
# 7. Build host uploader tool
# 8. Generate artifacts and build report
```

**Build time:** Approximately 30-45 minutes on a modern system (first build includes toolchain compilation).

### Artifacts

After building, all outputs are collected in the `artifacts/` directory:

```
artifacts/
├── host/
│   └── fw_upload              # Firmware uploader tool
├── gateware/
│   └── ice40_picorv32.bin     # FPGA bitstream
├── firmware/
│   ├── led_blink.bin          # LED blink demo
│   ├── timer_clock.bin        # Real-time clock
│   ├── hexedit.bin            # Interactive hex editor
│   ├── mandelbrot_float.bin   # Mandelbrot set (floating point)
│   ├── mandelbrot_fixed.bin   # Mandelbrot set (fixed point)
│   ├── algo_test.bin          # Algorithm tests
│   ├── heap_test.bin          # Dynamic memory test
│   └── ...                    # More examples
└── build-report.txt           # Comprehensive build report
```

A versioned tarball is also created: `artifacts/olimex-ice40hx8k-picorv32-<tag>-<timestamp>.tar.gz`

## Usage

### Programming the FPGA

Use the Olimex programmer tool to load the bitstream:

```bash
# Program FPGA with bitstream
olimexino-32u4 -p artifacts/gateware/ice40_picorv32.bin

# Or use iceprog if available
iceprog artifacts/gateware/ice40_picorv32.bin
```

### Connecting to the Bootloader

The bootloader runs automatically after FPGA configuration:

```bash
# Connect via minicom (Linux)
minicom -D /dev/ttyUSB0 -b 115200

# Or screen
screen /dev/ttyUSB0 115200

# Or picocom
picocom -b 115200 /dev/ttyUSB0
```

**Bootloader commands:**
- `help` - Show available commands
- `upload` - Enter firmware upload mode
- `jump` - Execute uploaded firmware
- `read <addr>` - Read memory
- `write <addr> <data>` - Write memory

### Uploading Firmware

Use the `fw_upload` host tool to send firmware to the bootloader:

```bash
# List available serial ports
artifacts/host/fw_upload --list

# Upload firmware
artifacts/host/fw_upload -p /dev/ttyUSB0 artifacts/firmware/led_blink.bin

# Upload with verbose output
artifacts/host/fw_upload -p /dev/ttyUSB0 artifacts/firmware/timer_clock.bin -v
```

The uploader features:
- Beautiful progress bar with speed/ETA
- Rotating ACK protocol for reliability
- CRC32 verification
- Cross-platform (Linux, macOS, Windows)

## Firmware Examples

The project includes over 20 example firmware applications:

### Basic Examples
- **led_blink.c** - Simple LED blinker
- **uart_echo_test.c** - UART echo test
- **button_demo.c** - Button input handling

### Newlib C Standard Library
- **printf_test.c** - Interactive printf/scanf test menu
- **stdio_test.c** - Basic stdio operations
- **heap_test.c** - Dynamic memory allocation (malloc/free)
- **math_test.c** - Standard math library functions

### Advanced Applications
- **timer_clock.c** - Real-time clock with timer peripheral
- **hexedit.c** - Interactive hex editor with curses-like interface
- **mandelbrot_float.c** - Mandelbrot set with floating-point math
- **mandelbrot_fixed.c** - Mandelbrot set with fixed-point math
- **algo_test.c** - Algorithm tests (sorting, searching)

### FreeRTOS Real-Time Operating System

**NEW!** Full FreeRTOS RTOS integration with custom PicoRV32 port:

- **freertos_minimal.c** - Minimal FreeRTOS test (creates 1 task, validates xTaskCreate)
- **freertos_demo.c** - Multi-task demo with 4 tasks (LED blink + UART status)
- **freertos_printf_demo.c** - Printf-based demo using newlib (floating point formatting)

**Features:**
- Custom FreeRTOS port for PicoRV32's non-standard interrupt system
- Preemptive multitasking at 1 KHz tick rate (1ms resolution)
- Context switching using PicoRV32 custom instructions (maskirq, getq, retirq)
- Static linking with newlib C library for full printf() support
- Configurable via Kconfig (CPU clock, tick rate, heap size, priorities)
- Heap: 16 KB FreeRTOS heap (configurable), separate from newlib heap
- All standard FreeRTOS features: tasks, queues, semaphores, mutexes, timers

**Quick Start:**
```bash
# Enable FreeRTOS in configuration
make defconfig
echo "CONFIG_FREERTOS=y" >> .config

# Download FreeRTOS kernel
make freertos-download

# Build demos
make fw-freertos-minimal       # Minimal test (12.5 KB)
make fw-freertos-demo          # Multi-task demo (14 KB)
make fw-freertos-printf-demo   # Printf demo (28 KB)

# Upload to FPGA
tools/uploader/fw_upload_fast firmware/freertos_printf_demo.bin
```

**freertos_printf_demo** demonstrates:
- Task 1: Counter (2s period, decimal and hex formatting)
- Task 2: Float demo (3s period, %.4f formatting)
- Task 3: System status (5s period, tick count, heap usage)
- All tasks use printf() from newlib with full format support (%d, %u, %lu, %f)
- LED toggling on each task iteration
- Proper use of FreeRTOS macros (portNOP, vTaskDelay, etc.)

**Memory Usage:**
- freertos_minimal: ~13 KB (minimal FreeRTOS overhead)
- freertos_demo: ~14 KB (custom UART functions)
- freertos_printf_demo: ~28 KB (includes full newlib printf)

**Configuration (Kconfig):**
All FreeRTOS settings configurable via `make menuconfig` or `.config`:
- CPU clock frequency (default: 50 MHz)
- Tick rate (default: 1000 Hz = 1ms)
- Max task priorities (default: 5)
- Minimum stack size (default: 128 words = 512 bytes)
- Total heap size (default: 16 KB, range: 4-200 KB)
- Optional features (vTaskDelay, uxTaskPriorityGet, etc.)

See `lib/freertos_port/` for PicoRV32-specific port implementation and `lib/freertos_config/FreeRTOSConfig.h` for configuration.

### Testing & Verification
- **interactive.c** - Interactive peripheral test
- **irq_timer_test.c** - Timer interrupt test
- **syscall_test.c** - Syscall bridge verification
- **verify_algo.c** - Algorithm verification suite
- **verify_math.c** - Math library verification

## Project Structure

```
.
├── hdl/                    # HDL source files
│   ├── ice40_picorv32_top.v      # Top-level module
│   ├── picorv32.v                # PicoRV32 CPU core
│   ├── sram_driver_new.v         # 5-cycle SRAM controller
│   ├── uart.v                    # UART peripheral
│   ├── timer_peripheral.v        # Timer peripheral
│   ├── mmio_peripherals.v        # MMIO controller
│   ├── firmware_loader.v         # Firmware upload state machine
│   ├── bootloader_rom.v          # Bootloader ROM
│   ├── ice40_picorv32.pcf        # Pin constraints
│   └── ice40_picorv32.sdc        # Timing constraints
│
├── bootloader/             # Bootloader source
│   ├── bootloader.c              # Main bootloader
│   └── Makefile
│
├── firmware/               # Firmware examples
│   ├── *.c                       # Example applications
│   └── Makefile
│
├── lib/                    # Support libraries
│   ├── syscalls/                 # Newlib syscalls (UART I/O)
│   ├── simple_upload/            # Firmware upload protocol
│   ├── microrl/                  # Command-line parser
│   └── incurses/                 # Curses-like terminal library
│
├── tools/                  # Host utilities
│   └── uploader/
│       ├── fw_upload.c           # Cross-platform uploader
│       └── Makefile
│
├── scripts/                # Build scripts
│   ├── verify-platform.sh        # Platform verification
│   └── ...
│
├── configs/                # Configuration files
├── build/                  # Build artifacts (gitignored)
├── downloads/              # Downloaded dependencies (gitignored)
├── artifacts/              # Final outputs (gitignored)
├── Makefile                # Main build system
└── README.md               # This file
```

## Memory Map

```
0x00000000 - 0x00001FFF  (8KB)    Bootloader ROM
0x00002000 - 0x0007FFFF  (504KB)  Application SRAM
0x10000000 - 0x100000FF           MMIO Peripherals
  0x10000000                      UART data
  0x10000004                      UART status
  0x10000010                      Timer control
  0x10000014                      Timer value
  0x10000020                      GPIO
  0x10000030                      CRC32
  0x10000040                      Firmware upload control
```

## Build Targets

```bash
make                    # Build everything
make toolchain-check    # Verify platform and tools
make toolchain-download # Download FPGA tools
make toolchain          # Build RISC-V GCC (if needed)
make bootloader         # Build bootloader
make synthesis          # Synthesize HDL
make pnr                # Place and route
make bitstream          # Generate bitstream
make firmware           # Build all firmware
make uploader           # Build host uploader tool
make timing             # Run timing analysis
make artifacts          # Collect all outputs
make clean              # Remove build artifacts
make distclean          # Clean build/ and artifacts/
```

## Timing and Performance

The system meets all timing requirements with margin:

- **Target frequency**: 50.00 MHz (20.0 ns period)
- **Achieved frequency**: 63.95 MHz (15.64 ns period)
- **Timing margin**: 13.95 MHz (27.9%)

**FPGA utilization:**
- DFFs: 3080
- LUTs: 7054 / 7680 (92%)
- BRAMs: 18 / 32 (56%)
- Carry chains: 744

## Known Issues and Notes

### Yosys 0.58+ ABC9 Optimization
Yosys 0.58 and later have an ABC9 optimization issue that can cause CRC32 state machine corruption. The current build uses optimized SRAM controller with ABC9 disabled for stability. See `YOSYS_ABC9_ISSUE.md` for details.

### SRAM Controller
The system uses a 5-cycle SRAM access pattern for reliability. A 4-cycle optimized version exists but may have timing issues on some boards. The stable 5-cycle version is default.

## Development

### Adding New Firmware

1. Create your `.c` file in `firmware/`
2. Add your target to `firmware/Makefile`
3. Build with: `make TARGET=yourapp firmware`
4. Upload with: `artifacts/host/fw_upload -p /dev/ttyUSB0 firmware/yourapp.bin`

### Modifying HDL

1. Edit files in `hdl/`
2. Rebuild bitstream: `make synthesis pnr bitstream`
3. Program FPGA with new bitstream

### Using Newlib C Standard Library

Firmware applications can use standard C library functions (printf, scanf, malloc, etc.) by building with `USE_NEWLIB=1`:

```bash
make TARGET=yourapp USE_NEWLIB=1 firmware
```

The syscalls bridge in `lib/syscalls/` provides UART-based I/O for newlib.

## Credits and License

**Copyright (c) October 2025 Michael Wolak**
Email: mikewolak@gmail.com, mike@epromfoundry.com

**NOT FOR COMMERCIAL USE**
**EDUCATIONAL AND RESEARCH PURPOSES ONLY**

This project is provided as-is for educational and research purposes. Commercial use, redistribution for profit, or incorporation into commercial products is strictly prohibited without explicit written permission from the copyright holder.

### Third-Party Components

This project incorporates the following third-party components under their respective licenses:

- **PicoRV32** - Copyright (c) Clifford Wolf, ISC License
- **Newlib** - Various authors, BSD-style licenses
- **microrl** - Eugene Samoylov, BSD 3-Clause License

See individual component directories for their specific license terms.

## Minicom-FPGA Detailed Guide

**Minicom-FPGA 2.10.90-PicoRV32** is a custom terminal emulator with integrated FAST streaming protocol for ultra-fast firmware uploads to the iCE40HX8K FPGA.

### Why Minicom-FPGA?

Traditional firmware upload methods using external tools (fw_upload) require:
- Exiting your terminal session
- Running a separate upload tool
- Reconnecting to the terminal
- Multiple context switches

**Minicom-FPGA eliminates this friction** by integrating the FAST protocol directly into the terminal emulator. Upload firmware without ever leaving your terminal session!

### Features

- **Integrated FAST Protocol**: Built-in C implementation, no fork/exec overhead
- **90-104 KB/sec Upload Speed**:
  - Linux/PC: ~90 KB/sec
  - macOS: ~104 KB/sec
  - Windows: ~86 KB/sec
- **Ultra-Efficient Protocol**: Only 3 ACKs for entire transfer (vs 4096+ in standard protocol)
- **1 Mbaud UART**: 8.7x faster than standard 115200 baud
- **Single Protocol**: Streamlined menu-free experience
- **Cross-Platform**: Linux and macOS support with portable build system

### Building Minicom-FPGA

```bash
cd tools/minicom-picorv32
./build.sh
```

The build script:
- Detects your platform (Linux/macOS)
- Uses local autopoint/gettext tools (no system dependencies)
- Compiles minicom with FAST protocol integration
- Installs to `build/bin/minicom`

**Build time:** ~1 minute on modern systems

### Usage

**Connect to FPGA:**
```bash
src/minicom -D /dev/ttyUSB0 -b 1000000
```

**Upload firmware:**
1. Press `Ctrl-A` to enter command mode
2. Press `S` for send/upload
3. File browser appears - navigate and select your firmware file
4. Upload happens automatically with real-time progress
5. Returns to terminal when complete

**That's it!** No menu navigation, no external tools, no hassle.

### FAST Protocol Technical Details

The FAST streaming protocol is optimized for maximum throughput with minimal overhead:

**Protocol Sequence:**
1. PC sends 'R' (Ready) → FPGA responds 'A' (ACK)
2. PC sends 4-byte size → FPGA responds 'B' (ACK)
3. PC streams ALL data continuously (NO per-chunk ACKs!)
4. PC sends 'C' + 4-byte CRC32
5. FPGA calculates CRC, responds 'C' + calculated CRC32
6. PC verifies CRC match

**Key Innovation:** Step 3 streams the entire firmware in one continuous transfer with zero interruptions. Standard chunked protocols send ~64 bytes, wait for ACK, send next chunk, repeat 4096+ times.

**Performance Comparison:**

| Method | ACKs | Upload Time (256KB) | Speed |
|--------|------|---------------------|-------|
| Standard Bootloader | 4096+ | ~20 seconds | ~13 KB/sec |
| FAST Protocol (Linux) | 3 | ~2.3 seconds | ~90 KB/sec |
| FAST Protocol (macOS) | 3 | ~2.3 seconds | ~104 KB/sec |

### Components

**Minicom-FPGA includes:**
- `src/minicom` - Main terminal executable
- `src/fast-xfr.c` - FAST protocol implementation
- `src/updown.c` - Protocol handler integration
- `build.sh` - Portable build script

**Compatible with:**
- `bootloader/bootloader_fast.c` - FAST-enabled bootloader
- `firmware/hexedit_fast.c` - FAST-enabled firmware
- `tools/uploader/fw_upload_fast.c` - Standalone FAST uploader

### Troubleshooting

**Issue: Connection fails**
- Verify correct serial port: `ls /dev/ttyUSB*` or `ls /dev/tty.usbserial*`
- Check FPGA is programmed with bitstream
- Ensure FAST bootloader is loaded (not standard bootloader)

**Issue: Upload hangs**
- Verify 1 Mbaud baud rate: `src/minicom -D /dev/ttyUSB0 -b 1000000`
- FPGA must be running bootloader_fast or hexedit_fast firmware
- Standard bootloader won't work with FAST protocol

**Issue: CRC mismatch**
- Serial cable quality - try shorter cable
- Electromagnetic interference - check grounding
- Retry upload - transient errors are rare but possible

**Debug logging:** Check `/tmp/minicom-fast-debug.log` for detailed protocol trace

### Building for macOS

macOS requires gettext for the build:

```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install gettext
brew install gettext
export PATH="/opt/homebrew/opt/gettext/bin:$PATH"

# Build Minicom-FPGA
cd tools/minicom-picorv32
./build.sh
```

### Source Code

Minicom-FPGA is based on minicom 2.10.90 with custom modifications:
- Added FAST streaming protocol in `src/fast-xfr.c`
- Modified protocol handler in `src/updown.c`
- Removed external protocol dependencies
- Streamlined single-protocol configuration

All modifications are clearly marked and documented in the source code.

## Support and Contact

For questions, bug reports, or inquiries about commercial licensing:

Michael Wolak
mikewolak@gmail.com
mike@epromfoundry.com

GitHub: https://github.com/mikewolak/olimex-ice40hx8k-picorv32

## Acknowledgments

- Clifford Wolf for PicoRV32 and the open-source FPGA toolchain (Yosys, nextpnr, icestorm)
- Olimex for the iCE40HX8K-EVB development board
- The RISC-V community for the open ISA and toolchain support
- Minicom developers for the excellent terminal emulator base
