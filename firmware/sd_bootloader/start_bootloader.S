//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform - Bootloader Startup
// start.S - RISC-V Startup Code for Bootloader
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

/*
 * Bootloader Startup Code for PicoRV32 (RV32E)
 * Entry point: _start at 0x10000
 * CPU reset vector (PROGADDR_RESET) points here
 */

.section .text.start
.global _start

_start:
    /* Set up stack pointer (grows downward from 0x12000) */
    la sp, __stack_top

    /* Clear BSS section */
    la t0, __bss_start
    la t1, __bss_end
clear_bss:
    bge t0, t1, done_clear_bss
    sw zero, 0(t0)
    addi t0, t0, 4
    j clear_bss
done_clear_bss:

    /* Call bootloader main function */
    call main

    /* If bootloader returns, infinite loop (should never happen) */
loop_forever:
    j loop_forever

/* Jump to user firmware at address 0x0 */
.global jump_to_firmware
jump_to_firmware:
    /* a0 contains the target address (should be 0x0) */
    jr a0
