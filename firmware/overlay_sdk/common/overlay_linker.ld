/*==============================================================================
 * Olimex iCE40HX8K-EVB RISC-V Platform - Overlay Linker Script
 * overlay_linker.ld - Position-independent overlay memory layout
 *
 * Copyright (c) October 2025 Michael Wolak
 * Email: mikewolak@gmail.com, mike@epromfoundry.com
 *============================================================================*/

/*
 * Overlay Linker Script for Position-Independent Code (PIC)
 *
 * Memory Layout (from memory_config.h):
 *   0x00018000 - 0x00037FFF : Overlay code/data/bss (128 KB)
 *   0x00038000 - 0x00039FFF : Overlay stack (8 KB, grows down)
 *   0x0003A000 - 0x0003FFFF : Overlay heap (24 KB, grows up)
 *
 * Key Requirements:
 *   1. Position-independent code (-fPIC)
 *   2. No absolute addressing
 *   3. Entry point at overlay_start
 *   4. Clean return to caller via exit()
 */

/* Import memory configuration - these match memory_config.h */
OVERLAY_BASE      = 0x00018000;
OVERLAY_MAX_SIZE  = 128K;
OVERLAY_END       = OVERLAY_BASE + OVERLAY_MAX_SIZE;

OVERLAY_STACK_SIZE = 8K;
OVERLAY_STACK_BASE = OVERLAY_END;
OVERLAY_STACK_TOP  = OVERLAY_STACK_BASE + OVERLAY_STACK_SIZE;

OVERLAY_HEAP_BASE = OVERLAY_STACK_TOP;
OVERLAY_HEAP_END  = 0x00040000;  /* Bootloader base */

MEMORY
{
    /* Overlay code/data region */
    OVERLAY (rwx) : ORIGIN = OVERLAY_BASE, LENGTH = OVERLAY_MAX_SIZE

    /* Stack region (not allocated in binary, just for symbols) */
    STACK (rw)    : ORIGIN = OVERLAY_STACK_BASE, LENGTH = OVERLAY_STACK_SIZE

    /* Heap region (not allocated in binary, just for symbols) */
    HEAP (rw)     : ORIGIN = OVERLAY_HEAP_BASE, LENGTH = OVERLAY_HEAP_END - OVERLAY_HEAP_BASE
}

SECTIONS
{
    /* Set base address */
    . = OVERLAY_BASE;

    /* Entry point symbol */
    ENTRY(_start)

    /*==========================================================================
     * Code Section
     *========================================================================*/
    .text : {
        /* Startup code MUST be first */
        *(.text.start)

        /* Main entry point */
        PROVIDE(_overlay_entry = .);

        /* All other code */
        *(.text*)
        *(.gnu.linkonce.t.*)

        /* Alignment */
        . = ALIGN(4);

        /* Mark end of code */
        PROVIDE(_etext = .);
    } > OVERLAY

    /*==========================================================================
     * Read-Only Data Section
     *========================================================================*/
    .rodata : {
        *(.rodata*)
        *(.srodata*)
        *(.gnu.linkonce.r.*)

        /* String literals */
        *(.rodata.str*)

        . = ALIGN(4);
    } > OVERLAY

    /*==========================================================================
     * Initialized Data Section
     *========================================================================*/
    .data : {
        PROVIDE(_data_start = .);

        *(.data*)
        *(.sdata*)
        *(.gnu.linkonce.d.*)

        . = ALIGN(4);
        PROVIDE(_data_end = .);
    } > OVERLAY

    /*==========================================================================
     * Uninitialized Data Section (BSS)
     *========================================================================*/
    .bss : {
        PROVIDE(__bss_start = .);

        *(.bss*)
        *(.sbss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)

        . = ALIGN(4);
        PROVIDE(__bss_end = .);
    } > OVERLAY

    /*==========================================================================
     * Overlay Size Calculation and Validation
     *========================================================================*/

    /* End of overlay binary */
    PROVIDE(_overlay_end = .);

    /* Calculate total size */
    PROVIDE(_overlay_size = _overlay_end - OVERLAY_BASE);

    /* Verify overlay fits in allocated space */
    ASSERT(_overlay_size <= OVERLAY_MAX_SIZE, "ERROR: Overlay exceeds 128 KB limit!")
    ASSERT(. <= OVERLAY_END, "ERROR: Overlay extends past OVERLAY_END!")

    /*==========================================================================
     * Stack Configuration
     *========================================================================*/

    /* Stack grows down from OVERLAY_STACK_TOP */
    PROVIDE(__stack_top = OVERLAY_STACK_TOP);
    PROVIDE(__stack_bottom = OVERLAY_STACK_BASE);
    PROVIDE(__stack_size = OVERLAY_STACK_SIZE);

    /*==========================================================================
     * Heap Configuration
     *========================================================================*/

    /* Heap starts after BSS, grows up toward stack */
    PROVIDE(__heap_start = OVERLAY_HEAP_BASE);
    PROVIDE(__heap_end = OVERLAY_HEAP_END);
    PROVIDE(__heap_size = OVERLAY_HEAP_END - OVERLAY_HEAP_BASE);

    /* Verify we have reasonable heap space */
    ASSERT(__heap_size >= 4096, "ERROR: Heap is less than 4KB!")

    /*==========================================================================
     * Debug Sections (not loaded, only for debugging)
     *========================================================================*/

    .comment 0 : { *(.comment) }
    .debug 0 : { *(.debug) }
    .line 0 : { *(.line) }
    .debug_srcinfo 0 : { *(.debug_srcinfo) }
    .debug_sfnames 0 : { *(.debug_sfnames) }
    .debug_aranges 0 : { *(.debug_aranges) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_line 0 : { *(.debug_line .debug_line.* .debug_line_end) }
    .debug_frame 0 : { *(.debug_frame) }
    .debug_str 0 : { *(.debug_str) }
    .debug_loc 0 : { *(.debug_loc) }
    .debug_macinfo 0 : { *(.debug_macinfo) }
    .debug_weaknames 0 : { *(.debug_weaknames) }
    .debug_funcnames 0 : { *(.debug_funcnames) }
    .debug_typenames 0 : { *(.debug_typenames) }
    .debug_varnames 0 : { *(.debug_varnames) }
    .debug_pubtypes 0 : { *(.debug_pubtypes) }
    .debug_ranges 0 : { *(.debug_ranges) }
    .debug_macro 0 : { *(.debug_macro) }

    /*==========================================================================
     * Discard Sections
     *========================================================================*/

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.gnu_debuglink)
        *(.gnu.lto_*)
    }
}

/*==============================================================================
 * Memory Layout Summary (for reference)
 *============================================================================*/

/*
    Overlay Memory Map:

    ┌─────────────────────────────────────┐ 0x18000 (OVERLAY_BASE)
    │  .text (code)                       │
    ├─────────────────────────────────────┤
    │  .rodata (constants)                │
    ├─────────────────────────────────────┤
    │  .data (initialized data)           │
    ├─────────────────────────────────────┤
    │  .bss (uninitialized data)          │
    ├─────────────────────────────────────┤ _overlay_end (max 0x38000)
    │  /// Free space ///                 │
    ├─────────────────────────────────────┤ 0x38000 (OVERLAY_STACK_BASE)
    │  Stack (8 KB)                       │
    │  ↓↓↓ grows down ↓↓↓                 │
    ├─────────────────────────────────────┤ 0x3A000 (OVERLAY_STACK_TOP)
    │  Heap (24 KB)                       │
    │  ↑↑↑ grows up ↑↑↑                   │
    └─────────────────────────────────────┘ 0x40000 (OVERLAY_HEAP_END)

    Assertions:
    - _overlay_size <= 128 KB (OVERLAY_MAX_SIZE)
    - _overlay_end <= 0x38000 (OVERLAY_END)
    - __heap_size >= 4 KB (minimum usable heap)
*/
