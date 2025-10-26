# Overlay System - Optimized Memory Allocation

## Current Firmware Sizes (from build)

```
Firmware               Size     % of 256KB
------------------------------------------------
slip_http_server.bin   123 KB   48%
iperf_server.bin       110 KB   43%
slip_echo_server.bin   111 KB   43%
tcp_perf_server.bin    105 KB   41%
math_test.bin           84 KB   33%
sd_card_manager.bin     80 KB   31%  ← Our app
hexedit_fast.bin        75 KB   29%
stdio_test.bin          47 KB   18%
mandelbrot_*.bin        49 KB   19%
spi_test.bin            65 KB   25%
```

**Observation**: Even the LARGEST firmware (slip_http_server) is only 123 KB = **48% of available space**.

**Conclusion**: We can allocate overlay space much more generously!

---

## Proposed Optimized Memory Layout

```
Total SRAM: 512 KB (0x00000000 - 0x0007FFFF)

Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x0001FFFF| 128 KB  | Main firmware (sd_card_manager)
0x00020000 - 0x0003FFFF| 128 KB  | Overlay execution space #1
0x00040000 - 0x00041FFF|   8 KB  | Bootloader (BRAM/ROM)
0x00042000 - 0x0005FFFF| 120 KB  | Overlay execution space #2
0x00060000 - 0x0007FFFF| 128 KB  | Heap/Stack for SD Manager
```

**Rationale**:
1. **SD Manager (128 KB)**: Current size is 80 KB, leaves 48 KB headroom for features
2. **Overlay Space #1 (128 KB)**: Can fit even the largest current firmware!
3. **Overlay Space #2 (120 KB)**: Additional overlay or data space
4. **Heap/Stack (128 KB)**: Plenty for SD Manager operations, FatFS buffers

---

## Alternative: Simple Two-Region Layout

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x0001FFFF| 128 KB  | Main firmware (sd_card_manager)
0x00020000 - 0x0003FFFF| 128 KB  | Overlay execution space
0x00040000 - 0x00041FFF|   8 KB  | Bootloader (BRAM/ROM)
0x00042000 - 0x0007FFFF| 248 KB  | Heap/Stack (upload buffer + runtime)
```

**Benefits**:
- Clean, simple layout
- 128 KB for overlays (more than any current firmware)
- 248 KB heap for:
  - 128 KB upload buffer
  - 120 KB remaining for malloc/stack

---

## Recommended: "Just Enough" Layout

Since SD Manager is 80 KB and unlikely to exceed 96 KB even with overlay features:

```
Address Range          | Size    | Usage
-----------------------|---------|----------------------------------
0x00000000 - 0x00017FFF|  96 KB  | Main firmware (sd_card_manager)
0x00018000 - 0x0003FFFF| 160 KB  | Overlay execution space
0x00040000 - 0x00041FFF|   8 KB  | Bootloader (BRAM/ROM)
0x00042000 - 0x0007FFFF| 248 KB  | Heap/Stack
```

**Advantages**:
- **160 KB for overlays** - 30% larger than biggest current firmware
- Could run multiple overlays simultaneously!
- **248 KB heap** - massive upload buffer + runtime space

---

## Overlay Size Analysis

### What can fit in 128 KB?

```
✓ hexedit_fast (75 KB) + mandelbrot (49 KB) = 124 KB  ← Both at once!
✓ slip_http_server (123 KB)                           ← Biggest app
✓ Any combination of smaller apps
```

### What can fit in 160 KB?

```
✓ slip_http_server (123 KB) + 37 KB for another overlay
✓ 3x hexedit_fast (75 KB × 3 = 225 KB)  ← NO, too big
✓ hexedit (75 KB) + mandelbrot (49 KB) + spi_test (65 KB) = 189 KB  ← NO
✓ hexedit (75 KB) + mandelbrot (49 KB) + stdio_test (47 KB) = 171 KB ← NO
✓ hexedit (75 KB) + spi_test (65 KB) = 140 KB  ← YES!
```

**Conclusion**: 160 KB allows two medium-sized overlays simultaneously.

---

## Multi-Overlay Support

With 160 KB overlay space, we can support **multiple overlays loaded at once**:

### Layout Example

```
Overlay Space (160 KB @ 0x18000)
┌─────────────────────────────────┐
│ Slot 1: 0x18000 - 0x2FFFF (96KB)│  ← Large overlay
├─────────────────────────────────┤
│ Slot 2: 0x30000 - 0x3FFFF (64KB)│  ← Small overlay
└─────────────────────────────────┘
```

### Use Cases

**Scenario 1**: Hex Editor + Utilities
- Slot 1: hexedit_fast.bin (75 KB)
- Slot 2: calculator.bin (16 KB)
- Total: 91 KB / 160 KB (56% used)

**Scenario 2**: Network Tools
- Slot 1: tcp_server.bin (105 KB)
- Slot 2: diagnostics.bin (32 KB)
- Total: 137 KB / 160 KB (85% used)

**Scenario 3**: Games!
- Slot 1: tetris.bin (40 KB)
- Slot 2: snake.bin (24 KB)
- Slot 3: pong.bin (20 KB)
- Total: 84 KB / 160 KB (52% used)

---

## Updated Linker Scripts

### SD Manager (`linker.ld`)

```ld
MEMORY
{
    APPSRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 0x00018000  /* 96 KB */
    STACK (rw)    : ORIGIN = 0x00060000, LENGTH = 0x00020000  /* 128 KB */
}

SECTIONS
{
    ENTRY(_start)

    .text : {
        *(.text.start)
        *(.text*)
        . = ALIGN(4);
    } > APPSRAM

    .rodata : { *(.rodata*) . = ALIGN(4); } > APPSRAM
    .data   : { *(.data*) . = ALIGN(4); } > APPSRAM
    .bss    : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > APPSRAM

    __heap_start = 0x00042000;  /* After bootloader */
    __heap_end   = 0x00060000;  /* Before stack */
    __stack_top  = ORIGIN(STACK) + LENGTH(STACK);

    ASSERT(__app_size <= 0x00018000, "ERROR: Application exceeds 96 KB!")
}
```

### Overlay (`overlay_linker.ld`)

```ld
MEMORY {
    /* Primary overlay slot: 96 KB */
    OVERLAY1 (rwx) : ORIGIN = 0x00018000, LENGTH = 0x00018000

    /* Secondary overlay slot: 64 KB */
    OVERLAY2 (rwx) : ORIGIN = 0x00030000, LENGTH = 0x00010000
}

SECTIONS {
    . = 0x00018000;  /* Default: Slot 1 */

    .text : {
        *(.text.overlay_entry)
        *(.text*)
        . = ALIGN(4);
    } > OVERLAY1

    .rodata : { *(.rodata*) . = ALIGN(4); } > OVERLAY1
    .data   : { *(.data*) . = ALIGN(4); } > OVERLAY1
    .bss    : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > OVERLAY1

    __overlay_end = .;
    __overlay_size = __overlay_end - 0x00018000;

    ASSERT(__overlay_size <= 0x00018000, "ERROR: Overlay exceeds 96 KB!")
}
```

---

## Heap Usage Breakdown

### Total Heap: 120 KB (0x42000 - 0x60000)

```
Usage                    Size      Address Range
----------------------------------------------
Upload buffer            96 KB     0x42000 - 0x59FFF
FatFS work buffer         4 KB     0x5A000 - 0x5AFFF
SD Manager malloc pool   20 KB     0x5B000 - 0x5FFFF
```

**Why 96 KB upload buffer?**
- Matches largest overlay slot
- Can receive any overlay in one go
- Still leaves 24 KB for runtime allocation

---

## Comparison

### Original Proposal (from OVERLAY_SYSTEM.md)

```
Main firmware:  256 KB  ← Wasteful (only need 96 KB)
Overlay space:  192 KB  ← Good, but leaves only 56 KB heap
Heap:            56 KB  ← Too small for 192 KB upload buffer!
```

**Problem**: Can't upload 192 KB overlay with only 56 KB heap!

### Optimized Layout (This Document)

```
Main firmware:   96 KB  ← Right-sized (80 KB + headroom)
Overlay space:  160 KB  ← Huge! Can fit largest app + another
Heap:           120 KB  ← Enough for 96 KB upload + 24 KB runtime
```

**Solution**: Balanced allocation based on actual firmware sizes.

---

## Memory Map Visual

```
        512 KB SRAM
┌───────────────────────┐ 0x00000000
│                       │
│   SD Card Manager     │ 96 KB
│   (currently 80 KB)   │
│                       │
├───────────────────────┤ 0x00018000
│                       │
│   Overlay Slot 1      │ 96 KB
│   (large apps)        │
│                       │
├───────────────────────┤ 0x00030000
│                       │
│   Overlay Slot 2      │ 64 KB
│   (medium apps)       │
│                       │
├───────────────────────┤ 0x00040000
│   Bootloader (ROM)    │ 8 KB
├───────────────────────┤ 0x00042000
│                       │
│   Upload Buffer       │ 96 KB
│                       │
├───────────────────────┤ 0x0005A000
│   FatFS Buffer        │ 4 KB
├───────────────────────┤ 0x0005B000
│   Malloc Pool         │ 20 KB
├───────────────────────┤ 0x00060000
│                       │
│   Stack (grows down)  │ 128 KB
│                       │
└───────────────────────┘ 0x00080000
```

---

## Implementation Constants

```c
// In overlay_loader.h

// Memory regions
#define SD_MANAGER_BASE     0x00000000
#define SD_MANAGER_SIZE     (96 * 1024)

#define OVERLAY_SLOT1_BASE  0x00018000
#define OVERLAY_SLOT1_SIZE  (96 * 1024)

#define OVERLAY_SLOT2_BASE  0x00030000
#define OVERLAY_SLOT2_SIZE  (64 * 1024)

#define BOOTLOADER_BASE     0x00040000
#define BOOTLOADER_SIZE     (8 * 1024)

#define HEAP_BASE           0x00042000
#define UPLOAD_BUFFER_BASE  0x00042000
#define UPLOAD_BUFFER_SIZE  (96 * 1024)

#define FATFS_BUFFER_BASE   0x0005A000
#define FATFS_BUFFER_SIZE   (4 * 1024)

#define MALLOC_POOL_BASE    0x0005B000
#define MALLOC_POOL_SIZE    (20 * 1024)

#define STACK_BASE          0x00060000
#define STACK_SIZE          (128 * 1024)
```

---

## Benefits of This Layout

1. **Realistic Sizing**: Based on actual firmware measurements
2. **Generous Overlays**: 160 KB = 30% larger than biggest app
3. **Multi-Overlay**: Can load 2+ overlays simultaneously
4. **Adequate Heap**: 120 KB for uploads + runtime
5. **Future-Proof**: Still 32 KB headroom in SD Manager region
6. **Clean Boundaries**: All regions 4 KB aligned

---

## Recommendations

### For Phase 1-3 (Simple Single Overlay)

Use **Slot 1 only** (96 KB @ 0x18000):
- Simple implementation
- Handles all current firmware sizes
- Easy to understand

### For Phase 4+ (Advanced Multi-Overlay)

Use **both slots**:
- Load large overlay to Slot 1
- Load utility overlays to Slot 2
- Overlay manager tracks which slots are in use

---

## Author

Michael Wolak
October 2025

Based on real firmware size analysis from build output.
