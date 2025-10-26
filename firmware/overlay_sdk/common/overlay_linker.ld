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

/*
 * Memory configuration constants (must match memory_config.h)
 * Note: Linker scripts can't include C headers, so values are duplicated here
 */

MEMORY
{
    OVERLAY (rwx) : ORIGIN = 0x18000, LENGTH = 0x20000
    STACK   (rw)  : ORIGIN = 0x38000, LENGTH = 0x02000
    HEAP    (rw)  : ORIGIN = 0x3A000, LENGTH = 0x06000
}

SECTIONS
{
    /* Set base address to 0x18000 (overlay execution base) */
    . = 0x00018000;

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
    PROVIDE(_overlay_size = _overlay_end - 0x00018000);

    /* Verify overlay fits in allocated space (128KB = 0x20000) */
    ASSERT(_overlay_size <= 0x00020000, "ERROR: Overlay exceeds 128 KB limit!")
    ASSERT(. <= 0x00038000, "ERROR: Overlay extends past 0x38000!")

    /*==========================================================================
     * Stack Configuration
     *========================================================================*/

    /* Stack grows down from 0x3A000 (top of 8KB stack region) */
    PROVIDE(__stack_top = 0x0003A000);
    PROVIDE(__stack_bottom = 0x00038000);
    PROVIDE(__stack_size = 0x00002000);  /* 8KB */

    /*==========================================================================
     * Heap Configuration
     *========================================================================*/

    /* Heap: 0x3A000 - 0x40000 (24KB) */
    PROVIDE(__heap_start = 0x0003A000);
    PROVIDE(__heap_end = 0x00040000);
    PROVIDE(__heap_size = 0x00006000);  /* 24KB */

    /* Verify we have reasonable heap space (at least 4KB) */
    ASSERT(__heap_size >= 0x00001000, "ERROR: Heap is less than 4KB!")

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
