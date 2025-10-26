# Overlay Build System - PicoRV32 FPGA Platform

Position-independent code (PIC) overlays for dynamic loading and execution from SD card.

## Overview

This directory contains the build infrastructure for creating **overlay binaries** that can be:
1. Compiled as position-independent code
2. Uploaded to SD card via UART
3. Loaded into RAM at runtime
4. Executed without reflashing the FPGA

## Quick Start

### Building Overlays

```bash
cd firmware/overlays

# Build all overlays
make

# Build specific overlay
make hello.bin

# Clean
make clean
```

### Uploading to Hardware

1. **Build SD Card Manager** (if not already done):
```bash
cd ../
make sd_card_manager
```

2. **Build minicom-picorv32** (one time, if not already built):
```bash
cd ../../tools/minicom-picorv32
./build.sh
# Binary will be at: src/minicom
```

3. **Flash FPGA and upload firmware** (one time):
```bash
# Flash FPGA bitstream
iceprog ../../build/gateware/ice40_picorv32.bin

# Upload SD card manager firmware using minicom
cd ../../tools/minicom-picorv32
src/minicom -D /dev/ttyUSB0 -b 1000000
# Press Ctrl-A, then S, navigate to firmware/sd_card_manager.bin
# Upload happens automatically, then exit minicom (Ctrl-A, X)
```

4. **Insert SD card** and use SD Card Manager to detect/mount it

5. **Upload overlay via UART**:
   - In SD Card Manager menu, select "4. Upload Overlay (UART)"
   - Enter filename (e.g., `HELLO.BIN`)
   - From another terminal:
     ```bash
     # Using minicom (recommended)
     ../../tools/minicom-picorv32/src/minicom -D /dev/ttyUSB0 -b 1000000
     # Press Ctrl-A, S, select overlays/hello.bin

     # OR using fw_upload_fast
     ../../tools/uploader/fw_upload_fast -p /dev/ttyUSB0 hello.bin
     ```

6. **Run overlay**:
   - In SD Card Manager menu, select "5. Browse & Run Overlays"
   - Select `HELLO.BIN` and press ENTER

## Memory Layout

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x00017FFF|  96 KB  | SD Card Manager firmware
0x00018000 - 0x0002FFFF|  96 KB  | Overlay execution space
0x00030000 - 0x0003FFFF|  64 KB  | Reserved (future overlays)
0x00040000 - 0x00041FFF|   8 KB  | Bootloader (ROM)
0x00042000 - 0x0005FFFF| 120 KB  | Heap (upload buffer)
0x00060000 - 0x0007FFFF| 128 KB  | Stack
```

**Overlays execute at**: `0x00018000`
**Maximum overlay size**: 96 KB

## Creating a New Overlay

### Template

```c
#include "overlay_common.h"

void my_overlay_main(void) {
    // Initialize BSS section
    overlay_init_bss();

    // Your code here
    overlay_puts("Hello from overlay!\n");

    // Blink LEDs
    for (int i = 0; i < 10; i++) {
        LED_REG = i & 0x03;
        overlay_delay(1000000);
    }

    // Return to SD Card Manager
}

// Define entry point (REQUIRED!)
OVERLAY_ENTRY(my_overlay_main)
```

### Build

Add to `Makefile`:
```makefile
OVERLAYS = hello.bin myoverlay.bin
```

Then:
```bash
make myoverlay.bin
```

## Available Functions (overlay_common.h)

### UART I/O

```c
overlay_putc(char c)              // Print single character
overlay_puts(const char *s)       // Print string
overlay_getc(void)                // Read character (blocking)
overlay_print_hex(uint32_t val)   // Print hex number
overlay_print_dec(uint32_t val)   // Print decimal number
```

### Hardware Access

```c
LED_REG = 0x03;                   // Control LEDs (bits 0-1)
overlay_delay(uint32_t cycles)    // Busy wait delay
```

### Initialization

```c
overlay_init_bss()                // Clear .bss section (call first!)
```

## Position-Independent Code (PIC) Details

### Why PIC?

Overlays are compiled to run at `0x18000`, but standard RISC-V code has **absolute addresses** hard-coded. PIC allows the same binary to run at any address.

### Compiler Flags

```makefile
-fPIC                  # Position-independent code
-fno-plt               # No procedure linkage table
-fno-jump-tables       # No jump tables
-mno-relax             # Preserve PIC (no linker relaxation)
```

### Limitations

Because overlays are bare-metal PIC:
- ❌ No `printf` (use `overlay_puts`)
- ❌ No `malloc` (use static or stack allocation)
- ❌ No C library functions (implement your own)
- ✅ Direct hardware access (UART, LEDs, Timer)
- ✅ Small and fast

## Example Overlays

### hello.bin
Simple test overlay that:
- Prints "Hello World"
- Blinks LEDs
- Exits after key press or timeout

**Size**: 1.3 KB
**Use**: Testing overlay system

## Troubleshooting

### Overlay won't compile
- Check that `overlay_common.h` is included
- Verify `OVERLAY_ENTRY()` macro is used
- Make sure no C library functions are called

### Overlay won't execute
- Verify CRC32 matches (SD Manager prints this)
- Check that overlay size < 96 KB
- Ensure `overlay_init_bss()` is called first

### Overlay crashes
- Check stack usage (no stack overflow)
- Don't use uninitialized global variables
- Call `overlay_init_bss()` at start

### Can't upload to SD card
- Ensure SD card is mounted (menu option 1)
- Check `/OVERLAYS` directory exists
- Verify UART connection works

## Advanced: Converting Existing Firmware to Overlay

To convert an existing firmware app to an overlay:

1. **Remove dependencies**:
   - Replace `printf()` with `overlay_puts()`
   - Remove C library calls
   - Make all allocations static

2. **Add overlay wrapper**:
```c
#include "overlay_common.h"

extern void original_main(void);  // Your original main

void overlay_wrapper(void) {
    overlay_init_bss();
    original_main();
}

OVERLAY_ENTRY(overlay_wrapper)
```

3. **Build with overlay Makefile**:
```bash
make SOURCES="original_main.c wrapper.c" myapp.bin
```

## File Structure

```
overlays/
├── Makefile                 - Build system
├── overlay_linker.ld        - Linker script for PIC
├── overlay_common.h         - Common functions and macros
├── README.md                - This file
├── hello.c                  - Hello world example
└── hello.bin                - Built overlay binary
```

## Future Enhancements

- [ ] Multiple overlay slots (96KB + 64KB)
- [ ] Inter-overlay communication
- [ ] Shared library functions
- [ ] Overlay hot-reload
- [ ] Network upload of overlays

## References

- **SD Card Manager**: `firmware/sd_fatfs/sd_card_manager.c`
- **Overlay Loader**: `firmware/sd_fatfs/overlay_loader.c`
- **Upload Protocol**: `firmware/sd_fatfs/overlay_upload.c`
- **Memory Allocation**: `firmware/sd_fatfs/MEMORY_ALLOCATION.md`

## Author

Michael Wolak
Email: mikewolak@gmail.com, mike@epromfoundry.com
Date: October 2025

## License

Educational and research purposes.
