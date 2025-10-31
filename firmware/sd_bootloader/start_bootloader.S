//==============================================================================
// SD Bootloader Startup Code
//
// Minimal startup for bootloader that runs from BRAM at 0x10000.
// No interrupt support needed - this is a simple sequential bootloader.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

.section .text.start
.global _start

_start:
    // Initialize stack pointer
    la sp, __stack_top

    // Clear BSS section
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bge t0, t1, bss_done
    sw zero, 0(t0)
    addi t0, t0, 4
    j clear_bss

bss_done:
    // Call main bootloader function
    call main

    // If main returns, loop forever
loop_forever:
    j loop_forever
