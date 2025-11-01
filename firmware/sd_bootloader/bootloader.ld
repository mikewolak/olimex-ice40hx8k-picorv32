/*==============================================================================
 * SD Bootloader Linker Script
 *
 * This bootloader will be burned into the HDL bitstream and runs from BRAM ROM.
 * It loads the main bootloader from SD card sectors 1-375 to SRAM at 0x0,
 * then jumps to 0x0 to execute it.
 *
 * CRITICAL: Must match UART bootloader memory layout exactly!
 *
 * Physical Memory Map (as seen by CPU):
 *   0x00000000 - 0x0003FFFF : Main firmware SRAM (256KB)
 *   0x00040000 - 0x00041FFF : Bootloader ROM (8KB, mapped by mem_controller)
 *   0x00042000 - 0x0007FFFF : Heap/Stack SRAM (~248KB)
 *   0x80000000+            : MMIO peripherals
 *
 * Linking Strategy (SAME AS UART BOOTLOADER):
 *   VMA (Virtual/Execution Address) = 0x40000 (where CPU executes from)
 *   LMA (Load Memory Address)       = 0x00000 (for hex file, ROM module indexing)
 *
 *   The bootloader ROM module is 8KB, indexed 0x0000-0x1FFF internally.
 *   CPU boots at physical 0x40000 (PROGADDR_RESET), which maps to ROM.
 *   We use AT() to separate execution address from hex file address.
 *
 * Copyright (c) October 2025 Michael Wolak
 * Email: mikewolak@gmail.com, mike@epromfoundry.com
 *==============================================================================*/

MEMORY
{
    /* Bootloader ROM: VMA at 0x40000 (execution), LMA at 0x0 (hex file) */
    BOOTROM_EXEC (rx) : ORIGIN = 0x00040000, LENGTH = 8K
    BOOTROM_LOAD (rx) : ORIGIN = 0x00000000, LENGTH = 8K

    /* BSS data in SRAM (must start AFTER ROM ends at 0x42000) */
    BOOTSRAM (rw)     : ORIGIN = 0x00042000, LENGTH = 3K

    /* Stack in SRAM */
    STACK (rw)        : ORIGIN = 0x00042F00, LENGTH = 256
}

SECTIONS
{
    ENTRY(_start)

    /* Code section: Execute at 0x40000, load from 0x0 */
    .text : AT(ADDR(.text) - 0x40000) {
        *(.text.start)      /* start.S must be first */
        *(.text*)
        . = ALIGN(4);
    } > BOOTROM_EXEC

    /* Read-only data */
    .rodata : AT(ADDR(.rodata) - 0x40000) {
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > BOOTROM_EXEC

    /* Initialized data */
    .data : AT(ADDR(.data) - 0x40000) {
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
    } > BOOTROM_EXEC

    /* BSS in SRAM (separate memory region) */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > BOOTSRAM

    /* Stack pointer */
    __stack_top = ORIGIN(STACK) + LENGTH(STACK);

    /* Verify ROM size */
    __bootrom_size = SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data);
    ASSERT(__bootrom_size <= 8K, "ERROR: Bootloader exceeds 8KB ROM!")
}
