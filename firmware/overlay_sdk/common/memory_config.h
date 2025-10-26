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
// System Memory Layout
//==============================================================================

// Total SRAM: 512 KB
#define TOTAL_SRAM_SIZE         (512 * 1024)
#define SRAM_BASE               0x00000000
#define SRAM_END                (SRAM_BASE + TOTAL_SRAM_SIZE)

//==============================================================================
// Main Firmware Region (SD Card Manager, etc.)
//==============================================================================

#define FIRMWARE_BASE           0x00000000
#define FIRMWARE_SIZE           (256 * 1024)    // 256 KB for main firmware
#define FIRMWARE_END            (FIRMWARE_BASE + FIRMWARE_SIZE)

//==============================================================================
// Bootloader Region (BRAM/ROM)
//==============================================================================

#define BOOTLOADER_BASE         0x00040000
#define BOOTLOADER_SIZE         (8 * 1024)      // 8 KB bootloader ROM
#define BOOTLOADER_END          (BOOTLOADER_BASE + BOOTLOADER_SIZE)

//==============================================================================
// Overlay Execution Region
//==============================================================================

// Overlay code/data region
#define OVERLAY_BASE            0x00018000      // 96 KB into SRAM
#define OVERLAY_MAX_SIZE        (128 * 1024)    // 128 KB max overlay size
#define OVERLAY_END             (OVERLAY_BASE + OVERLAY_MAX_SIZE)

// Overlay stack (grows DOWN from OVERLAY_STACK_TOP)
#define OVERLAY_STACK_SIZE      (8 * 1024)      // 8 KB stack
#define OVERLAY_STACK_BASE      OVERLAY_END     // Stack starts where code ends
#define OVERLAY_STACK_TOP       (OVERLAY_STACK_BASE + OVERLAY_STACK_SIZE)

// Overlay heap (grows UP from OVERLAY_HEAP_BASE)
#define OVERLAY_HEAP_BASE       OVERLAY_STACK_TOP
#define OVERLAY_HEAP_END        BOOTLOADER_BASE
#define OVERLAY_HEAP_SIZE       (OVERLAY_HEAP_END - OVERLAY_HEAP_BASE)

//==============================================================================
// Main Firmware Heap/Stack Region
//==============================================================================

// Used by main firmware (SD Card Manager)
#define MAIN_HEAP_BASE          BOOTLOADER_END  // After bootloader ROM
#define MAIN_HEAP_END           SRAM_END
#define MAIN_HEAP_SIZE          (MAIN_HEAP_END - MAIN_HEAP_BASE)

//==============================================================================
// Memory Map Summary
//==============================================================================

/*
  Address Range          | Size    | Usage
  -----------------------|---------|----------------------------------
  0x00000000 - 0x0003FFFF| 256 KB  | Main firmware (SD Card Manager)
  0x00018000 - 0x00037FFF| 128 KB  | Overlay code/data/bss
  0x00038000 - 0x00039FFF|   8 KB  | Overlay stack (grows down)
  0x0003A000 - 0x0003FFFF|  24 KB  | Overlay heap (grows up)
  0x00040000 - 0x00041FFF|   8 KB  | Bootloader (BRAM/ROM)
  0x00042000 - 0x0007FFFF| 248 KB  | Main firmware heap/stack

  Visual Layout:

  ┌─────────────────────────────────────┐ 0x00000000
  │  Main Firmware                      │
  │  (SD Card Manager, etc.)            │ 256 KB
  │                                     │
  │  ┌───────────────────────────────┐  │ 0x00018000
  │  │ Overlay Code/Data/BSS         │  │ 128 KB
  │  ├───────────────────────────────┤  │ 0x00038000
  │  │ Overlay Stack (↓↓↓)           │  │ 8 KB
  │  ├───────────────────────────────┤  │ 0x0003A000
  │  │ Overlay Heap (↑↑↑)            │  │ 24 KB
  │  └───────────────────────────────┘  │ 0x00040000
  ├─────────────────────────────────────┤
  │  Bootloader (BRAM/ROM)              │ 8 KB
  ├─────────────────────────────────────┤ 0x00042000
  │  Main Firmware Heap/Stack           │ 248 KB
  └─────────────────────────────────────┘ 0x00080000
*/

//==============================================================================
// Validation Checks (compile-time assertions)
//==============================================================================

// Ensure overlay doesn't overlap with main firmware end (0x40000)
#if (OVERLAY_BASE < FIRMWARE_END && OVERLAY_END > FIRMWARE_END)
#error "ERROR: Overlay region overlaps with main firmware!"
#endif

// Ensure overlay heap doesn't overlap with bootloader
#if (OVERLAY_HEAP_END > BOOTLOADER_BASE)
#error "ERROR: Overlay heap extends into bootloader region!"
#endif

// Ensure we have at least some heap space
#if (OVERLAY_HEAP_SIZE < (4 * 1024))
#error "ERROR: Overlay heap is less than 4KB - increase OVERLAY_MAX_SIZE or OVERLAY_STACK_SIZE"
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
