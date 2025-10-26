/******************************************************************************
 * Overlay Linker Script - Position Independent Code
 *
 * This linker script is used for building overlay binaries that execute from
 * RAM at address 0x18000 (after main firmware).
 *
 * Key Features:
 * - Position-independent code (PIC) support
 * - Load address: 0x00018000 (96 KB after firmware start)
 * - Maximum size: 96 KB
 * - Entry point at start of .text section
 *
 * Copyright (c) October 2025 Michael Wolak
 * Email: mikewolak@gmail.com, mike@epromfoundry.com
 ******************************************************************************/

MEMORY
{
    /* Overlay execution space - 96 KB after main firmware */
    RAM (rwx) : ORIGIN = 0x00018000, LENGTH = 0x00018000  /* 96 KB */
}

ENTRY(_overlay_start)

SECTIONS
{
    . = 0x00018000;

    /* Entry point section (must be first) */
    .text.overlay_entry : {
        *(.text.overlay_entry)  /* Overlay entry point goes here */
        . = ALIGN(4);
    } > RAM

    /* Code section */
    .text : {
        *(.text.start)
        *(.text*)
        *(.text)
        . = ALIGN(4);
    } > RAM

    /* Read-only data */
    .rodata : {
        *(.rodata*)
        *(.rodata)
        . = ALIGN(4);
    } > RAM

    /* Initialized data */
    .data : {
        __data_start = .;
        *(.data*)
        *(.data)
        *(.sdata*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM

    /* Uninitialized data */
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(.bss)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /* End of overlay */
    . = ALIGN(4);
    __overlay_end = .;
    __overlay_size = __overlay_end - 0x00018000;

    /* Discard unwanted sections */
    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note*)
    }

    /* Assertions */
    ASSERT(__overlay_size <= 0x00018000, "ERROR: Overlay exceeds 96 KB!")
}
