# Hello World Overlay - Test Project

**First bare-metal overlay for the Overlay SDK**

## Overview

This is a simple test overlay that demonstrates the basic overlay system functionality:
- Position-independent code (PIC)
- Bare-metal execution (no libc dependencies)
- Clean return to SD Card Manager
- UART output using hardware I/O functions

## Build Status

✅ **Built successfully** - 432 bytes

Memory usage:
- Text: 432 bytes
- Data: 0 bytes
- BSS: 4 bytes
- **Total: 436 bytes** (0.3% of 128KB limit)

## What It Does

Prints "Hello World!" to UART 10 times with delays between each iteration, then returns cleanly to the SD Card Manager menu.

## How to Build

```bash
cd firmware/overlay_sdk/projects/hello_world
make clean
make all
```

Generated files:
- `hello_world.bin` - Upload this to SD card
- `hello_world.elf` - Executable with debug symbols
- `hello_world.map` - Linker map file

## How to Test

### Upload to SD Card

1. Build the SD Card Manager firmware:
   ```bash
   cd firmware
   make sd_card_manager
   ```

2. Upload and run SD Card Manager on FPGA

3. In SD Card Manager menu, select "Upload Overlay"

4. Upload `hello_world.bin` using your terminal's file send feature

### Run the Overlay

1. In SD Card Manager menu, select "Browse and Run Overlays"

2. Navigate to `hello_world.BIN`

3. Press ENTER to execute

4. Should see:
   ```
   Hello World!
   Hello World!
   ... (10 times total)
   ```

5. Should return cleanly to SD Card Manager menu

## Technical Details

### Memory Layout

```
0x20000 - 0x201B0  : Code (.text)     432 bytes
0x201B0 - 0x201B4  : BSS               4 bytes
0x38000 - 0x3A000  : Stack (8KB, grows down from 0x3A000)
0x3A000 - 0x40000  : Heap (24KB, not used in this overlay)
```

### Position-Independent Code

The overlay uses PC-relative addressing throughout:

```assembly
_start:
    lui sp, 0x3A              # Stack at 0x3A000
    auipc t0, %pcrel_hi(__bss_start)   # PC-relative BSS access
    jalr ra, %pcrel_lo(4b)(ra)         # PC-relative function call
```

### Bare-Metal Functions Used

From `io.c` (bare-metal implementations):
- `putchar(c)` - Outputs single character to UART
- `exit(status)` - Clean return to caller

From `overlay_start.S`:
- Stack setup at 0x3A000
- BSS clearing
- Call main()
- Handle return via exit()

## Next Steps

After this overlay is verified working on hardware:

1. Build PIC sysroot with newlib for printf support
2. Create enhanced hello_world using printf, malloc, etc.
3. Create more complex overlays (games, utilities, etc.)

## Notes

- This is a bare-metal overlay (no libc)
- Uses only hardware I/O functions
- Extremely compact (432 bytes)
- Perfect for validating the overlay system works
- Foundation for future overlay development
