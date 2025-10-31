/*==============================================================================
 * SD Bootloader Linker Script
 *
 * This bootloader will be burned into the HDL bitstream and runs from BRAM.
 * It loads the main bootloader from SD card sectors 1-375 to SRAM at 0x0,
 * then jumps to 0x0 to execute it.
 *
 * Memory Layout:
 * - BRAM (8KB at 0x10000): Bootloader code (this program)
 * - SRAM (512KB at 0x0): Target for loading main bootloader
 *
 * Copyright (c) October 2025 Michael Wolak
 * Email: mikewolak@gmail.com, mike@epromfoundry.com
 *==============================================================================*/

MEMORY
{
    /* Bootloader runs from BRAM at 0x10000 (8KB available) */
    BOOTRAM (rwx) : ORIGIN = 0x00010000, LENGTH = 0x00002000

    /* Stack in high SRAM (won't conflict with loading data at 0x0) */
    STACK (rw)    : ORIGIN = 0x0007F000, LENGTH = 0x00001000  /* 4KB stack at top of SRAM */
}

SECTIONS
{
    ENTRY(_start)

    /* Code section */
    .text : {
        *(.text.start)      /* Startup code first */
        *(.text*)
        . = ALIGN(4);
    } > BOOTRAM

    /* Read-only data */
    .rodata : {
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > BOOTRAM

    /* Initialized data */
    .data : {
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
    } > BOOTRAM

    /* Uninitialized data */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > BOOTRAM

    /* Stack pointer (grows down from top of SRAM) */
    __stack_top = 0x00080000;  /* Top of 512KB SRAM */

    /* Verify bootloader fits in 8KB BRAM */
    __bootloader_size = SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss);
    ASSERT(__bootloader_size <= 0x00002000, "ERROR: Bootloader exceeds 8KB BRAM!")
}
