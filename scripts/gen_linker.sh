#!/bin/bash
# Generate linker.ld from .config

set -e

if [ ! -f .config ]; then
    echo "ERROR: .config not found. Run 'make menuconfig' or 'make defconfig' first."
    exit 1
fi

source .config

mkdir -p build/generated

cat > build/generated/linker.ld << EOF
/* Auto-generated from .config - DO NOT EDIT */
/* Generated: $(date) */

MEMORY
{
    APPSRAM (rwx) : ORIGIN = ${CONFIG_APP_SRAM_BASE:-0x00000000}, LENGTH = ${CONFIG_APP_SRAM_SIZE:-0x00040000}
    STACK (rw)    : ORIGIN = 0x00074000, LENGTH = 0x0000C000  /* 48KB stack (3x safety margin) */
}

SECTIONS
{
    ENTRY(_start)

    /* Code section at 0x0 */
    .text : {
        *(.text.start)      /* Startup code first */
        *(.text*)
        . = ALIGN(4);
    } > APPSRAM

    /* Read-only data */
    .rodata : {
        *(.rodata*)
        *(.srodata*)
        . = ALIGN(4);
    } > APPSRAM

    /* Initialized data */
    .data : {
        *(.data*)
        *(.sdata*)
        . = ALIGN(4);
    } > APPSRAM

    /* CRITICAL: Overlay Communication Section at Fixed Address 0x2A000
     * This section MUST be at this exact address for overlay timer IRQ handlers.
     * Overlays write their IRQ handler function pointer to this location,
     * and the firmware's irq_handler() calls it during timer interrupts.
     * MUST come BEFORE .bss to reserve the address before linker allocates BSS.
     */
    . = 0x0002A000;
    .overlay_comm 0x0002A000 : {
        KEEP(*(.overlay_comm))
        . = ALIGN(4);
    } > APPSRAM

    /* Uninitialized data */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > APPSRAM

    /* Heap starts after BSS, extends to stack */
    __heap_start = ALIGN(., 4);
    __heap_end = ORIGIN(STACK);  /* Heap ends at 0x74000, ~200KB+ available for buffers */

    /* Stack pointer (grows down from top of SRAM) */
    __stack_top = 0x00080000;  /* Top of 512KB SRAM */

    /* Verify application fits in SRAM */
    __app_size = SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss);
    ASSERT(__app_size <= ${CONFIG_APP_SRAM_SIZE:-0x00040000}, "ERROR: Application exceeds SRAM!")
}
EOF

echo "✓ Generated build/generated/linker.ld"
