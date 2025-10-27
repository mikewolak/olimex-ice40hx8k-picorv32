//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform - Memory Configuration
// memory_config.h - Central memory layout definitions for overlays
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef MEMORY_CONFIG_H
#define MEMORY_CONFIG_H

//==============================================================================
// System Memory Layout - CORRECTED ADDRESSES
//==============================================================================

// Total SRAM: 512 KB
#define TOTAL_SRAM_SIZE         (512 * 1024)
#define SRAM_BASE               0x00000000
#define SRAM_END                (SRAM_BASE + TOTAL_SRAM_SIZE)

//==============================================================================
// Main Firmware Region (SD Card Manager)
//==============================================================================

// Main firmware measured at ~124KB (0x1E63C)
// Allocate 256KB for firmware + heap + stack + upload buffer
#define FIRMWARE_BASE           0x00000000
#define FIRMWARE_SIZE           (256 * 1024)    // 256 KB for firmware/heap/stack
#define FIRMWARE_END            (FIRMWARE_BASE + FIRMWARE_SIZE)

//==============================================================================
// Upload Buffer Region (used during overlay upload)
//==============================================================================

// Upload buffer is part of main firmware's heap space
// Used temporarily during overlay upload, then freed
#define UPLOAD_BUFFER_BASE      0x0001E640      // After SD manager code
#define UPLOAD_BUFFER_SIZE      (128 * 1024)    // 128 KB max upload
#define UPLOAD_BUFFER_END       (UPLOAD_BUFFER_BASE + UPLOAD_BUFFER_SIZE)

//==============================================================================
// Bootloader Region (BRAM/ROM)
//==============================================================================

#define BOOTLOADER_BASE         0x00040000
#define BOOTLOADER_SIZE         (8 * 1024)      // 8 KB bootloader ROM
#define BOOTLOADER_END          (BOOTLOADER_BASE + BOOTLOADER_SIZE)

//==============================================================================
// Main Firmware Heap/Stack Region
//==============================================================================

// Heap starts after bootloader, stack grows down from 0x5F000
// 4KB safety gap (0x5F000-0x60000) between stack and overlay
#define MAIN_HEAP_BASE          BOOTLOADER_END  // 0x42000
#define MAIN_HEAP_END           0x0005F000      // Before stack top
#define MAIN_HEAP_SIZE          (MAIN_HEAP_END - MAIN_HEAP_BASE)

#define MAIN_STACK_TOP          0x0005F000      // Stack grows down, 4KB gap before overlay
#define OVERLAY_SAFETY_GAP      (4 * 1024)      // 4KB gap between stack and overlay

//==============================================================================
// Overlay Execution Region - AFTER all main firmware memory
//==============================================================================

// Overlay placed at 0x60000 (384KB) - well clear of main firmware
// This is after:
//   - Main firmware code/data/bss (~124KB)
//   - Upload buffer (128KB)
//   - Heap space (120KB)
//   - Stack space

#define OVERLAY_BASE            0x00060000      // 384 KB into SRAM
#define OVERLAY_MAX_SIZE        (96 * 1024)     // 96 KB max overlay size
#define OVERLAY_END             (OVERLAY_BASE + OVERLAY_MAX_SIZE)

// Overlay stack (grows DOWN from OVERLAY_STACK_TOP)
#define OVERLAY_STACK_SIZE      (8 * 1024)      // 8 KB stack
#define OVERLAY_STACK_BASE      OVERLAY_END     // Stack starts where code ends
#define OVERLAY_STACK_TOP       (OVERLAY_STACK_BASE + OVERLAY_STACK_SIZE)

// Overlay heap (grows UP from OVERLAY_HEAP_BASE)
#define OVERLAY_HEAP_BASE       OVERLAY_STACK_TOP
#define OVERLAY_HEAP_END        SRAM_END
#define OVERLAY_HEAP_SIZE       (OVERLAY_HEAP_END - OVERLAY_HEAP_BASE)

//==============================================================================
// Memory Map Summary
//==============================================================================

/*
  Address Range          | Size    | Usage
  -----------------------|---------|----------------------------------
  0x00000000 - 0x0001E63C| 124 KB  | Main firmware (SD Card Manager)
  0x0001E640 - 0x0003E63F| 128 KB  | Upload buffer (temporary)
  0x00040000 - 0x00041FFF|   8 KB  | Bootloader (BRAM/ROM)
  0x00042000 - 0x0005EFFF| 116 KB  | Main firmware heap
  0x0005F000 - Stack top (grows down)
  0x0005F000 - 0x0005FFFF|   4 KB  | SAFETY GAP (stack/overlay separation)
  0x00060000 - 0x00077FFF|  96 KB  | Overlay code/data/bss
  0x00078000 - 0x00079FFF|   8 KB  | Overlay stack (grows down)
  0x0007A000 - 0x0007FFFF|  24 KB  | Overlay heap (grows up)

  Visual Layout:

  ┌─────────────────────────────────────┐ 0x00000000
  │  Main Firmware (SD Card Manager)    │ ~124 KB
  ├─────────────────────────────────────┤ 0x0001E640
  │  Upload Buffer (temporary)          │ 128 KB
  ├─────────────────────────────────────┤ 0x00040000
  │  Bootloader (BRAM/ROM)              │ 8 KB
  ├─────────────────────────────────────┤ 0x00042000
  │  Main Firmware Heap                 │ 116 KB
  ├─────────────────────────────────────┤ 0x0005F000 (stack top)
  │  *** SAFETY GAP *** (4KB)           │ 4 KB
  ├─────────────────────────────────────┤ 0x00060000
  │  Overlay Code/Data/BSS              │ 96 KB
  ├─────────────────────────────────────┤ 0x00078000
  │  Overlay Stack (↓↓↓)                │ 8 KB
  ├─────────────────────────────────────┤ 0x0007A000
  │  Overlay Heap (↑↑↑)                 │ 24 KB
  └─────────────────────────────────────┘ 0x00080000

  Key Points:
  - CRITICAL: 4KB safety gap between main stack and overlay prevents corruption
  - Main firmware stack grows down from 0x5F000 (not 0x60000!)
  - Overlay at 0x60000 (384KB) is AFTER main firmware memory + gap
  - No overlap between main firmware heap and overlay region
  - Upload buffer is temporary, freed before overlay execution
  - 24KB overlay heap is adequate for malloc/newlib
*/

//==============================================================================
// Validation Checks (compile-time assertions)
//==============================================================================

// Ensure overlay doesn't overlap with main firmware heap
#if (OVERLAY_BASE < MAIN_HEAP_END)
#error "ERROR: Overlay region overlaps with main firmware heap!"
#endif

// Ensure overlay heap doesn't exceed SRAM
#if (OVERLAY_HEAP_END > SRAM_END)
#error "ERROR: Overlay heap extends beyond SRAM!"
#endif

// Ensure we have at least some heap space
#if (OVERLAY_HEAP_SIZE < (4 * 1024))
#error "ERROR: Overlay heap is less than 4KB!"
#endif

// Ensure overlay fits in available space
#if (OVERLAY_END > OVERLAY_STACK_TOP || OVERLAY_STACK_TOP > SRAM_END)
#error "ERROR: Overlay regions exceed available SRAM!"
#endif

//==============================================================================
// Helper Macros
//==============================================================================

// Convert size to KB for display
#define KB(bytes)               ((bytes) / 1024)

// Check if address is in overlay region
#define IS_IN_OVERLAY(addr)     ((addr) >= OVERLAY_BASE && (addr) < OVERLAY_END)

// Check if address is in overlay stack region
#define IS_IN_OVERLAY_STACK(addr) \
    ((addr) >= OVERLAY_STACK_BASE && (addr) < OVERLAY_STACK_TOP)

// Check if address is in overlay heap region
#define IS_IN_OVERLAY_HEAP(addr) \
    ((addr) >= OVERLAY_HEAP_BASE && (addr) < OVERLAY_HEAP_END)

#endif // MEMORY_CONFIG_H
