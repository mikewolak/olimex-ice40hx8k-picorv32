# SD Card Overlay System - Position Independent Code Loading

## Overview

The SD Card Manager will be extended to support uploading, storing, and executing position-independent code (PIC) modules from the SD card. This creates a dynamic overlay system for loading code at runtime without reflashing the FPGA.

## Motivation

**Problem**: FPGA code space is limited (256 KB). Many applications won't fit together.

**Solution**: Store additional code modules on SD card as "overlays" that can be:
1. Uploaded from PC via UART (using existing bootloader protocol)
2. Verified with CRC32 (proven protocol)
3. Saved to SD card filesystem
4. Loaded into heap RAM (248 KB available)
5. Executed as position-independent code

**Use Cases**:
- Development: Test new code without reflashing FPGA
- Applications: Load different utilities on demand (hex editor, network tools, games)
- Updates: Field-update overlay modules via SD card
- Education: Students can upload and run code experiments

---

## Architecture

### Memory Layout (Current)

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x0003FFFF| 256 KB  | Main firmware (sd_card_manager)
0x00040000 - 0x00041FFF| 8 KB    | Bootloader (BRAM/ROM)
0x00042000 - 0x0007FFFF| 248 KB  | Heap/Stack
```

### Proposed Overlay Memory Layout

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x0003FFFF| 256 KB  | Main firmware (sd_card_manager)
0x00040000 - 0x00041FFF| 8 KB    | Bootloader (BRAM/ROM)
0x00042000 - 0x00050000| 56 KB   | SD Card Manager heap
0x00050000 - 0x0007FFFF| 192 KB  | Overlay execution space
```

**Key Points**:
- SD Card Manager runs from 0x0 (position-dependent)
- Overlays load to 0x50000 and above (position-independent)
- Overlays can use up to ~192 KB of space
- Each overlay has dedicated stack space in its region

---

## SD Card Directory Structure

```
/OVERLAYS/              - Root directory for overlay modules
    HEXEDIT.BIN         - Hex editor overlay (hexedit_fast.c compiled as PIC)
    MANDEL.BIN          - Mandelbrot generator
    NETTEST.BIN         - Network diagnostics tool
    GAME.BIN            - Simple game
    TEST.BIN            - User test code

/OVERLAYS/INFO/         - Metadata for each overlay
    HEXEDIT.INF         - JSON or simple text metadata
    MANDEL.INF
    ...

/OVERLAYS/UPLOAD/       - Temporary upload staging area
    TEMP.BIN            - Currently uploading file
```

### Metadata Format (*.INF files)

Simple text format:
```
NAME=HexEdit Fast
DESC=Fast hex editor for memory editing
SIZE=65536
CRC32=0x12345678
ENTRY=0x50000
STACK=0x60000
VERSION=1.0
AUTHOR=Michael Wolak
```

---

## Upload Protocol (Reuse Bootloader Protocol)

The SD Card Manager will implement the **Simple Upload Protocol** already proven in:
- `bootloader_fast.c` (receiver)
- `fw_upload_fast.c` (sender)
- `simple_upload.c` (library)

### Protocol Flow

```
PC (fw_upload_fast)              SD Manager (simple_upload.c)
-------------------------------------------------------------------
1. Send 'R' (Ready)          --> Wait for 'R'
2. Wait for 'A'              <-- Send 'A' (ACK Ready)
3. Send 4-byte size          -->
4. Wait for 'B'              <-- Send 'B' (ACK Size)
5. Stream data chunks (64B)  --> Receive to heap buffer
   Send ACK after each chunk <-- Send 'C', 'D', 'E'... 'Z', 'A'...
6. Send 'C' + 4-byte CRC     -->
7. Wait for ACK + CRC        <-- Calculate CRC, send 'ACK' + CRC
8. Compare CRCs              --> If match, save to SD card
```

**Advantages**:
- Proven protocol (already used in bootloader)
- Robust CRC32 verification
- Chunk-based ACKs for reliability
- No new code on PC side (reuse `fw_upload_fast`)

### Heap Buffer Strategy

```c
// Allocate large buffer in heap for upload
#define OVERLAY_BUFFER_SIZE (192 * 1024)  // 192 KB max overlay
static uint8_t *overlay_buffer = NULL;

void init_overlay_system(void) {
    // Allocate from heap (starts at 0x42000)
    overlay_buffer = malloc(OVERLAY_BUFFER_SIZE);
    if (!overlay_buffer) {
        // Handle error
    }
}
```

---

## Position-Independent Code (PIC) Requirements

### Why PIC?

Overlays must run at **different addresses** (0x50000+) than they were compiled for.
Standard RISC-V code has **absolute addresses** baked in.

**Solution**: Compile overlays with `-fPIC` (Position Independent Code).

### Compiler Flags

```makefile
# For overlay modules (NOT for main firmware)
OVERLAY_CFLAGS = -march=rv32im -mabi=ilp32 \
                 -fPIC -fno-plt -fno-jump-tables \
                 -mno-relax -O2 -g

OVERLAY_LDFLAGS = -nostartfiles -static \
                  -T overlay_linker.ld \
                  -Wl,-Ttext=0x50000 \
                  -Wl,-Bsymbolic
```

### Overlay Linker Script

```ld
/* overlay_linker.ld - Position-independent overlay */
MEMORY {
    OVERLAY (rwx) : ORIGIN = 0x00050000, LENGTH = 0x00030000  /* 192 KB */
}

SECTIONS {
    . = 0x00050000;

    .text : {
        *(.text.overlay_entry)  /* Entry point first */
        *(.text*)
    } > OVERLAY

    .rodata : { *(.rodata*) } > OVERLAY
    .data   : { *(.data*) } > OVERLAY
    .bss    : { *(.bss*) } > OVERLAY

    __overlay_stack = 0x80000;  /* Use top of SRAM for stack */
}
```

### Entry Point Convention

Every overlay must provide a standard entry point:

```c
// overlay_template.c
void __attribute__((section(".text.overlay_entry"))) overlay_entry(void) {
    // Initialize overlay-specific hardware
    // Run main overlay code
    // Return to SD manager when done
}
```

### Calling Overlay from SD Manager

```c
// Load overlay from SD card to heap
uint32_t load_overlay(const char *filename) {
    FIL file;
    FRESULT res;
    UINT bytes_read;

    res = f_open(&file, filename, FA_READ);
    if (res != FR_OK) return 0;

    // Read entire file into overlay buffer
    res = f_read(&file, overlay_buffer, OVERLAY_BUFFER_SIZE, &bytes_read);
    f_close(&file);

    if (res != FR_OK) return 0;

    // Verify CRC from metadata
    // ... (read .INF file, compare CRC)

    return bytes_read;
}

// Execute overlay
void run_overlay(void) {
    // Function pointer to overlay entry point
    typedef void (*overlay_func_t)(void);
    overlay_func_t overlay_entry = (overlay_func_t)0x50000;

    // Jump to overlay
    overlay_entry();

    // Overlay returns here when done
}
```

---

## SD Manager Menu Integration

### New Menu Options

```
SD CARD MANAGER - Main Menu

 1. Detect SD Card
 2. Card Information
 3. Format Card (FAT32)
 4. File Browser
 5. Create Test File
 6. Read/Write Benchmark
 7. SPI Speed Configuration
 8. Eject Card

 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 OVERLAY SYSTEM
 ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

 9. Upload Overlay (via UART)
10. Browse Overlays
11. Run Overlay
12. Delete Overlay
13. Show Overlay Info
```

### Upload Overlay Flow

```
User selects: "9. Upload Overlay"
↓
SD Manager displays:
  "Ready to receive overlay via UART"
  "Use: fw_upload_fast -p /dev/ttyUSB0 hexedit.bin"
  "Press ESC to cancel"
↓
Calls simple_receive() to heap buffer
↓
Displays upload progress (bytes received)
↓
On completion:
  - Verify CRC32
  - Prompt for filename (8.3 format)
  - Save to /OVERLAYS/filename.BIN
  - Generate .INF metadata file
↓
Display: "✓ Overlay saved: HEXEDIT.BIN (65536 bytes)"
```

### Browse Overlays Flow

```
User selects: "10. Browse Overlays"
↓
Scan /OVERLAYS/ directory
↓
Display list with info from .INF files:
  ┌──────────────────────────────────┐
  │ Available Overlays               │
  ├──────────────────────────────────┤
  │ ▸ HEXEDIT.BIN   65 KB  v1.0     │
  │   MANDEL.BIN    48 KB  v1.2     │
  │   NETTEST.BIN   72 KB  v0.9     │
  └──────────────────────────────────┘

  ENTER: Run | D: Delete | I: Info | ESC: Back
```

### Run Overlay Flow

```
User selects overlay and presses ENTER
↓
Load overlay to heap (0x50000)
↓
Verify CRC32 from .INF file
↓
If valid:
  - Clear screen
  - Display: "Running HEXEDIT.BIN..."
  - Jump to overlay_entry()
↓
Overlay runs, uses UART, LEDs, etc.
↓
Overlay returns (or user presses reset button)
↓
SD Manager resumes
```

---

## Implementation Phases

### Phase 1: Upload Infrastructure (Week 1)
- [x] Integrate `simple_upload.c` into SD manager
- [ ] Add menu option "Upload Overlay"
- [ ] Test upload to heap buffer
- [ ] Save uploaded data to SD card
- [ ] Generate .INF metadata files

### Phase 2: Overlay Compiler Support (Week 1-2)
- [ ] Create `overlays/` directory structure
- [ ] Write `overlay_linker.ld` for PIC
- [ ] Create overlay Makefile with `-fPIC` flags
- [ ] Port `hexedit_fast.c` to overlay template
- [ ] Test compilation and verify relocatable code

### Phase 3: Overlay Execution (Week 2)
- [ ] Implement `load_overlay()` from SD to heap
- [ ] Verify CRC32 before execution
- [ ] Jump to overlay entry point
- [ ] Test overlay return to SD manager
- [ ] Handle overlay crashes gracefully

### Phase 4: Full Integration (Week 3)
- [ ] Browse overlays menu
- [ ] Run overlay menu
- [ ] Delete overlay functionality
- [ ] Overlay info display
- [ ] User documentation

### Phase 5: Advanced Features (Future)
- [ ] Overlay hot-reload (reload without restart)
- [ ] Multiple overlay support (chain loading)
- [ ] Overlay inter-communication (shared memory)
- [ ] Overlay permissions/sandboxing
- [ ] Network upload overlays (via lwIP)

---

## Security Considerations

### CRC32 Verification
- **ALWAYS verify CRC32** before executing overlay
- Compare uploaded CRC vs. stored .INF CRC
- Refuse to run if mismatch

### Stack Protection
- Each overlay gets dedicated stack region
- Check stack pointer before jumping
- Prevent overlay from corrupting SD manager

### Safe Return
- Overlay **MUST** return to SD manager
- Use watchdog timer to detect hung overlays
- Provide "force reset" escape hatch (button?)

### Malicious Code
- **WARNING**: No sandboxing in RISC-V bare metal
- Overlays have **full hardware access** (UART, SPI, LEDs, etc.)
- Only run **trusted code**
- Consider code signing for production

---

## Example: HexEdit as Overlay

### Current hexedit_fast.c
- Compiled to run at 0x0
- 75 KB binary
- Uses Simple Upload protocol

### As Overlay
```c
// hexedit_overlay.c
#include "overlay.h"

void __attribute__((section(".text.overlay_entry"))) overlay_entry(void) {
    // Initialize ncurses
    initscr();

    // Run hex editor main loop
    hexedit_main();

    // Clean up
    endwin();

    // Return to SD manager
    return;
}
```

**Compile**:
```bash
cd overlays
make hexedit_overlay
# Produces: HEXEDIT.BIN (position-independent)
```

**Upload**:
```bash
fw_upload_fast -p /dev/ttyUSB0 HEXEDIT.BIN
```

**Run**:
- SD Manager → Browse Overlays → HEXEDIT.BIN → ENTER
- Hex editor runs from 0x50000
- Edit memory, then quit
- Returns to SD Manager menu

---

## Testing Strategy

### Unit Tests
1. **Upload Protocol**: Test simple_upload.c with known data
2. **CRC32**: Verify CRC calculation matches fw_upload_fast
3. **File Save**: Upload → Save → Verify file contents on SD
4. **Load**: Load overlay from SD → Compare with original

### Integration Tests
1. **Small Overlay**: 1 KB "Hello World" overlay
2. **Medium Overlay**: 32 KB test program
3. **Large Overlay**: 128 KB full application
4. **Corrupt Data**: Intentionally corrupt CRC, verify rejection
5. **Memory Boundaries**: Overlay at max size (192 KB)

### Real-World Tests
1. Port `hexedit_fast.c` to overlay
2. Port `mandelbrot_float.c` to overlay
3. Create new overlay from scratch
4. Test on real SD card hardware

---

## Future Enhancements

### Dynamic Linking
- Provide SD manager functions to overlays (printf, UART, etc.)
- Use function pointer table at known address
- Overlays call SD manager services

### Overlay Marketplace
- Downloadable overlays from GitHub
- QR code for overlay download links
- Over-the-air updates via network

### Multi-Overlay Support
- Load multiple overlays simultaneously
- Overlay memory manager
- Inter-overlay communication

### Persistent Overlays
- Mark overlays as "autorun on boot"
- Overlay priority/scheduling
- Default overlay selection

---

## Directory Structure

```
firmware/
├── sd_fatfs/
│   ├── sd_card_manager.c       # Main app
│   ├── overlay_loader.c        # NEW: Load/execute overlays
│   ├── overlay_loader.h
│   ├── overlay_upload.c        # NEW: Upload via UART
│   ├── overlay_upload.h
│   └── simple_upload.c         # EXISTING: Protocol library
│
└── overlays/                    # NEW: Overlay modules
    ├── Makefile                 # Overlay build system
    ├── overlay_linker.ld        # PIC linker script
    ├── overlay.h                # Common overlay header
    ├── template/
    │   └── overlay_template.c   # Starting point for new overlays
    ├── hexedit/
    │   ├── hexedit_overlay.c    # Hex editor as overlay
    │   └── Makefile
    ├── mandelbrot/
    │   ├── mandelbrot_overlay.c
    │   └── Makefile
    └── README.md                # How to create overlays
```

---

## References

- **Bootloader Protocol**: `bootloader/bootloader_fast.c`
- **Upload Library**: `lib/simple_upload/simple_upload.c`
- **PC Uploader**: `tools/uploader/fw_upload_fast.c`
- **Memory Layout**: `firmware/linker.ld`
- **FatFS API**: `firmware/sd_fatfs/fatfs/source/ff.h`

---

## Author

Michael Wolak (mikewolak@gmail.com, mike@epromfoundry.com)
October 2025

---

## License

Educational and research purposes.
NOT FOR COMMERCIAL USE.
