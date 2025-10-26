# SD Card Overlay System - Executive Summary

## What Is This?

A **code loading system** that allows you to:
1. Upload compiled programs from your PC to the FPGA via UART
2. Store them on an SD card as "overlay modules"
3. Load and execute them on demand without reflashing the FPGA

Think of it as a **dynamic application loader** for bare-metal RISC-V.

---

## Why Is This Useful?

### Current Limitation
- FPGA has 256 KB code space
- Can only run ONE firmware at a time
- Changing programs requires reflashing (slow)

### With Overlays
- SD card stores MANY programs (megabytes)
- Switch between programs instantly
- Upload new code over UART (seconds, not minutes)
- Perfect for development and testing

---

## Use Cases

### Development
**Before**: Edit code → compile → flash FPGA (3-5 minutes) → test → repeat
**After**: Edit code → compile → upload via UART (10 seconds) → test → repeat

### Applications
- **Hex Editor**: Load when you need to edit memory
- **Network Tools**: TCP/IP utilities loaded on demand
- **Games**: Store multiple games, select from menu
- **Utilities**: Calculator, file viewer, benchmarks

### Field Updates
- Ship FPGA with SD card pre-loaded with overlays
- Users can update overlays by copying files to SD card
- No need for FPGA tools or special equipment

---

## How It Works

### Architecture

```
┌─────────────────────────────────────────────┐
│  PC  (Development Machine)                  │
│  ┌─────────────┐                           │
│  │ overlay.c   │  Compile with -fPIC        │
│  └──────┬──────┘                           │
│         │                                   │
│         ▼                                   │
│  ┌─────────────┐                           │
│  │ overlay.bin │  Position-independent code │
│  └──────┬──────┘                           │
└─────────┼─────────────────────────────────┘
          │
          │ Upload via UART (fw_upload_fast)
          │
          ▼
┌─────────────────────────────────────────────┐
│  FPGA (Olimex iCE40HX8K)                    │
│  ┌──────────────────────────────────────┐  │
│  │  SD Card Manager (at 0x0)            │  │
│  │  - Receives upload via UART          │  │
│  │  - Verifies CRC32                    │  │
│  │  - Saves to SD card /OVERLAYS/       │  │
│  │  - Later: loads to RAM (0x50000)     │  │
│  │  - Executes overlay                  │  │
│  └──────────────────────────────────────┘  │
│                                             │
│  Memory Layout:                             │
│  ┌──────────────────────────────────────┐  │
│  │ 0x00000 - 0x3FFFF : SD Manager (256K)│  │
│  ├──────────────────────────────────────┤  │
│  │ 0x40000 - 0x41FFF : Bootloader (8K)  │  │
│  ├──────────────────────────────────────┤  │
│  │ 0x50000 - 0x7FFFF : Overlay (192K)   │  │
│  └──────────────────────────────────────┘  │
│                                             │
│  ┌──────────────────────────────────────┐  │
│  │  SD Card                             │  │
│  │  /OVERLAYS/                          │  │
│  │    HEXEDIT.BIN  (65 KB)              │  │
│  │    MANDEL.BIN   (48 KB)              │  │
│  │    GAME.BIN     (32 KB)              │  │
│  │    ...                               │  │
│  └──────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
```

### Workflow

```
1. DEVELOP
   ┌─────────────┐
   │  Write      │
   │  overlay.c  │
   └──────┬──────┘
          │
          ▼
   ┌─────────────┐
   │  Compile    │  make overlay.bin
   │  with -fPIC │  (position-independent)
   └──────┬──────┘
          │
2. UPLOAD │
          ▼
   ┌──────────────────────┐
   │ fw_upload_fast       │  Upload via UART
   │  -p /dev/ttyUSB0     │  CRC verified
   │  overlay.bin         │
   └──────┬───────────────┘
          │
          ▼
   ┌──────────────────────┐
   │ SD Manager           │  Receives upload
   │  Saves to:           │  to heap buffer
   │  /OVERLAYS/NAME.BIN  │
   └──────┬───────────────┘
          │
3. RUN    │
          ▼
   ┌──────────────────────┐
   │ SD Manager Menu      │
   │  → Browse Overlays   │  User selects
   │  → Run Overlay       │  overlay to run
   └──────┬───────────────┘
          │
          ▼
   ┌──────────────────────┐
   │ Load from SD         │  Read file to
   │  → 0x50000           │  RAM @ 0x50000
   └──────┬───────────────┘
          │
          ▼
   ┌──────────────────────┐
   │ Execute!             │  Jump to 0x50000
   │  overlay_entry()     │  Run overlay code
   └──────┬───────────────┘
          │
          ▼
   ┌──────────────────────┐
   │ Return               │  Overlay returns
   │  → SD Manager        │  Back to menu
   └──────────────────────┘
```

---

## Key Technologies

### 1. Simple Upload Protocol
**Reuses proven bootloader protocol**:
- PC sends 'R' (ready)
- FPGA sends 'A' (ack)
- PC sends size (4 bytes)
- FPGA sends 'B' (ack)
- PC streams data in 64-byte chunks
- FPGA sends 'C', 'D', 'E'... (chunk acks)
- PC sends 'C' + CRC32 (4 bytes)
- FPGA verifies CRC, sends back calculated CRC
- If match: SUCCESS!

**Advantages**:
- Already implemented in `lib/simple_upload/`
- Already used by `fw_upload_fast` tool
- Robust CRC32 verification
- No new code needed!

### 2. Position-Independent Code (PIC)
**Problem**: Normal RISC-V code has absolute addresses
```asm
lui  a0, 0x80000      # Load 0x80000000 (absolute!)
lw   a1, 0(a0)        # Won't work if code moves
```

**Solution**: Compile with `-fPIC` (Position Independent Code)
```asm
auipc a0, 0           # Load PC-relative address
addi  a0, a0, offset  # Calculate relative offset
lw    a1, 0(a0)       # Works at ANY address!
```

**How**:
```makefile
CFLAGS += -fPIC -fno-plt -fno-jump-tables -mno-relax
LDFLAGS += -Wl,-Ttext=0x50000 -Wl,-Bsymbolic
```

### 3. FatFS Filesystem
**SD card stores overlays as regular files**:
- `/OVERLAYS/HEXEDIT.BIN` - Hex editor
- `/OVERLAYS/MANDEL.BIN` - Mandelbrot
- etc.

**Benefits**:
- Easy to manage (just copy files)
- Can remove SD card, add files on PC
- No special tools needed

---

## Implementation Status

### ✅ Already Working
- SD Card Manager with FatFS
- UART communication
- Simple Upload Protocol (`lib/simple_upload/`)
- fw_upload_fast tool
- Memory layout supports overlays

### 📋 Phase 1: Upload to SD (NEXT)
- Add "Upload Overlay" menu option
- Receive binary via UART to heap
- Save to `/OVERLAYS/` on SD card
- **Estimated**: 2-3 hours coding

### 📋 Phase 2: Build System
- Create `firmware/overlays/` directory
- Write overlay linker script (PIC)
- Create overlay Makefile
- Build test overlay (hello_world)
- **Estimated**: 2-3 hours

### 📋 Phase 3: Execution
- Load overlay from SD to 0x50000
- Jump to overlay entry point
- Return to SD Manager when done
- **Estimated**: 3-4 hours

### 📋 Phase 4: Full Integration
- Browse overlays menu
- Select and run from list
- Delete overlays
- Show overlay info
- **Estimated**: 4-6 hours

**Total Time**: ~2-3 days of development

---

## Example: Hello World Overlay

### 1. Write Code
```c
// hello_overlay.c
void overlay_entry(void) {
    // Direct hardware access (no linking needed!)
    volatile uint32_t *uart_tx = (volatile uint32_t *)0x80000000;
    volatile uint32_t *uart_status = (volatile uint32_t *)0x80000004;

    const char *msg = "Hello from overlay!\n";
    while (*msg) {
        while (*uart_status & 1);  // Wait
        *uart_tx = *msg++;         // Send
    }

    return;  // Back to SD Manager
}
```

### 2. Compile
```bash
cd firmware/overlays
make hello_overlay.bin
# Produces: hello_overlay.bin (~300 bytes)
```

### 3. Upload to FPGA
```bash
fw_upload_fast -p /dev/ttyUSB0 hello_overlay.bin
```

In SD Manager menu:
- Select "Upload Overlay"
- Enter filename: `HELLO.BIN`
- Wait for upload (1 second)
- ✓ Saved to /OVERLAYS/HELLO.BIN

### 4. Run
In SD Manager menu:
- Select "Run Overlay"
- Enter filename: `HELLO.BIN`
- See "Hello from overlay!" in terminal
- Returns to menu

---

## Comparison to Other Systems

### Arduino
- Uploads code via bootloader
- Replaces entire firmware
- **Overlay system**: Keeps SD Manager, loads modules

### Raspberry Pi
- Linux with dynamic libraries (.so)
- Kernel loads code on demand
- **Overlay system**: Bare-metal equivalent

### FPGA Partial Reconfiguration
- Reconfigure part of FPGA while running
- Complex, requires special tools
- **Overlay system**: Software-only, simple

---

## Advanced Features (Future)

### Dynamic Linking
- SD Manager provides services to overlays
- Function table at fixed address
- Overlays call: `printf()`, `uart_puts()`, etc.
- No need to duplicate code

### Multi-Overlay
- Load multiple overlays simultaneously
- Each in its own memory region
- Inter-overlay communication

### Overlay Marketplace
- Download overlays from GitHub
- QR codes for quick download
- Community-contributed modules

### Autorun
- Mark overlay as "boot default"
- Automatically run on startup
- User can override

---

## Security Notes

### CRC Verification
✅ **ALWAYS** verify CRC32 before execution
✅ Compare uploaded CRC vs file CRC
❌ **NEVER** run corrupted overlays

### Code Trust
⚠️ **WARNING**: Overlays have FULL hardware access
⚠️ No sandboxing in bare-metal RISC-V
⚠️ Only run code you trust
⚠️ Consider code signing for production

### Recovery
- Keep SD Manager always bootable
- Button to skip overlays (future)
- Watchdog timer for hung overlays

---

## Documentation

### Full Details
- **OVERLAY_SYSTEM.md** - Complete architecture and design
- **OVERLAY_IMPLEMENTATION.md** - Step-by-step implementation guide

### References
- **bootloader_fast.c** - Upload protocol reference
- **simple_upload.c** - Protocol library
- **fw_upload_fast.c** - PC-side uploader

---

## Getting Started

### 1. Read This Document
You just did! ✓

### 2. Read OVERLAY_IMPLEMENTATION.md
Step-by-step guide with code examples

### 3. Implement Phase 1
Add upload menu option (2-3 hours)

### 4. Test Upload
Upload a test file, verify saved to SD

### 5. Implement Phase 2
Create build system for overlays

### 6. Build Test Overlay
Compile hello_world.c to overlay.bin

### 7. Implement Phase 3
Load and execute overlay

### 8. Celebrate! 🎉
You now have a dynamic code loading system!

---

## Questions?

**Q: Does this replace the bootloader?**
A: No! Bootloader still loads SD Manager. Overlays are loaded BY SD Manager.

**Q: Can overlays call SD Manager functions?**
A: Not yet. Phase 1-3 are standalone. Future: dynamic linking.

**Q: What if overlay crashes?**
A: Currently: reset button. Future: watchdog timer.

**Q: How big can overlays be?**
A: Up to ~192 KB. Larger than most applications need.

**Q: Can overlays use newlib (printf, malloc)?**
A: Not yet. Phase 1-3 are bare-metal. Future: shared newlib.

**Q: Can I update overlays without removing SD card?**
A: Yes! Upload via UART replaces file on SD card.

---

## Author

Michael Wolak
mikewolak@gmail.com
mike@epromfoundry.com

October 2025

Educational and research purposes.
NOT FOR COMMERCIAL USE.
