//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform - Overlay Startup Code
// overlay_start.S - Position-Independent Startup for Overlays
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

/*
 * Overlay Startup Code - Position-Independent Implementation
 *
 * Key Requirements:
 *   1. Position-independent - works at load address (0x60000)
 *   2. Sets up stack at fixed address (0x7A000)
 *   3. Clears BSS using PC-relative addressing
 *   4. Calls main() with proper ABI
 *   5. Handles return via exit(0) to return to SD Card Manager
 *
 * Memory Layout (from overlay_linker.ld):
 *   Overlay code/data: 0x60000 - 0x77FFF (96 KB max)
 *   Stack:             0x78000 - 0x79FFF (8 KB, grows down from 0x7A000)
 *   Heap:              0x7A000 - 0x7FFFF (24 KB, grows up)
 *
 * Note: Overlays use fixed addresses within the reserved overlay region
 * (0x60000-0x80000). All code references must be PC-relative for correct
 * position-independent execution.
 */

.section .text.start
.global _start
.type _start, @function

_start:
    //==========================================================================
    // 1. Save Caller's Context to Fixed Memory Location
    //==========================================================================
    // CRITICAL: Must preserve caller's (SD card manager's) context!
    // Problem: We can't use caller's stack (might not have space)
    // Solution: Save to a fixed location in overlay region
    // Use 0x7FC00 (near end of overlay region, unlikely to conflict)

    // Save original SP and RA to fixed memory
    lui t0, 0x80          // t0 = 0x80000 (temporary)
    addi t0, t0, -1024    // t0 = 0x7FC00 (save area)
    sw sp, 0(t0)          // Save original SP
    sw ra, 4(t0)          // Save original RA

    //==========================================================================
    // 1.5. CRITICAL: Disable ALL Interrupts Immediately
    //==========================================================================
    // The SD card manager enables all interrupts before calling the overlay.
    // We MUST disable them immediately to prevent timer interrupts from firing
    // before the overlay has properly initialized its interrupt handlers.
    //
    // PicoRV32 maskirq instruction: mask=0xFFFFFFFF disables all interrupts
    // Encoding: .insn r 0x0B, 6, 3, rd, rs1, x0
    li t0, 0xFFFFFFFF     // t0 = ~0 (disable all)
    .insn r 0x0B, 6, 3, zero, t0, zero  // maskirq x0, t0

    //==========================================================================
    // 2. Set up Overlay Stack Pointer (Fixed Address)
    //==========================================================================
    // Stack is at 0x7A000 (OVERLAY_STACK_TOP from memory_config.h)
    // This is a fixed address, safe to use absolute addressing
    lui sp, 0x7a

    //==========================================================================
    // 3. Clear Frame Pointer
    //==========================================================================
    mv fp, zero

    //==========================================================================
    // 3. Clear BSS Section (Position-Independent)
    //==========================================================================
    // We must use PC-relative addressing since overlay can load anywhere

.option push
.option norelax                 // Disable linker relaxation for accurate PC-relative

    // Get __bss_start using PC-relative addressing
    // auipc loads PC + offset into register
1:  auipc t0, %pcrel_hi(__bss_start)
    addi t0, t0, %pcrel_lo(1b)

    // Get __bss_end using PC-relative addressing
2:  auipc t1, %pcrel_hi(__bss_end)
    addi t1, t1, %pcrel_lo(2b)

.option pop

clear_bss_loop:
    // Check if done
    bgeu t0, t1, clear_bss_done

    // Clear 4 bytes
    sw zero, 0(t0)

    // Advance pointer
    addi t0, t0, 4

    // Continue loop
    j clear_bss_loop

clear_bss_done:

    //==========================================================================
    // 4. Set up Global Pointer (GP) - Position-Independent
    //==========================================================================
    // The global pointer is used for efficient access to global variables
    // in the +/- 2KB range. Must be set to __global_pointer$ if defined.

.option push
.option norelax

    // Check if __global_pointer$ exists (provided by linker)
    .weak __global_pointer$

    // Load global pointer using PC-relative addressing
3:  auipc gp, %pcrel_hi(__global_pointer$)
    addi gp, gp, %pcrel_lo(3b)

.option pop

    //==========================================================================
    // 5. Initialize heap (if needed by malloc)
    //==========================================================================
    // Heap runs from __heap_start to __heap_end
    // Newlib's malloc will use sbrk() which needs these symbols
    // We just make them visible - newlib handles initialization

    //==========================================================================
    // 6. Call main()
    //==========================================================================
    // Arguments: argc = 0, argv = NULL, envp = NULL
    li a0, 0                    // argc = 0
    li a1, 0                    // argv = NULL
    li a2, 0                    // envp = NULL

    // Call main using PC-relative addressing
.option push
.option norelax
4:  auipc ra, %pcrel_hi(main)
    jalr ra, %pcrel_lo(4b)(ra)
.option pop

    //==========================================================================
    // 7. Handle Return from main()
    //==========================================================================
    // main() returned - call exit(return_value)
    // a0 already contains return value from main()

    // Call exit() to return cleanly to SD Card Manager
.option push
.option norelax
5:  auipc ra, %pcrel_hi(exit)
    jalr ra, %pcrel_lo(5b)(ra)
.option pop

    //==========================================================================
    // 8. Restore Caller's Context and Return
    //==========================================================================
    // CRITICAL: Restore original SP and RA!
    // Load from fixed memory location
    lui t0, 0x80          // t0 = 0x80000
    addi t0, t0, -1024    // t0 = 0x7FC00 (save area)
    lw sp, 0(t0)          // Restore original SP
    lw ra, 4(t0)          // Restore original RA

    // Return to SD Card Manager
    ret

    //==========================================================================
    // 9. If we somehow get here, hang
    //==========================================================================
_hang:
    j _hang

.size _start, . - _start

//==============================================================================
// Exit Handler - Called by newlib's exit()
//==============================================================================

.global _exit
.type _exit, @function

_exit:
    // This is called by newlib's exit()
    // For overlays, we want to return to the SD Card Manager
    //
    // Restore original context and return
    // Load from fixed memory location
    lui t0, 0x80          // t0 = 0x80000
    addi t0, t0, -1024    // t0 = 0x7FC00 (save area)
    lw sp, 0(t0)          // Restore original SP
    lw ra, 4(t0)          // Restore original RA

    // Return to SD Card Manager
    ret

.size _exit, . - _exit

//==============================================================================
// Weak Symbols for Newlib Stubs
//==============================================================================

// These may be needed by newlib but aren't implemented for overlays
// Provide weak symbols that just return

.weak _kill
.weak _getpid
.weak _write
.weak _read
.weak _close
.weak _lseek
.weak _fstat
.weak _isatty
.weak _sbrk

_kill:
_getpid:
_write:
_read:
_close:
_lseek:
_fstat:
_isatty:
    li a0, -1                  // Return error
    ret

// sbrk - simple implementation for heap management
_sbrk:
    // a0 = increment (bytes to allocate)
    // Returns: old break point (or -1 on error)

.option push
.option norelax
    // Get current heap break (static variable)
6:  auipc t0, %pcrel_hi(_heap_break)
    addi t0, t0, %pcrel_lo(6b)
    lw t1, 0(t0)                // Load current break

    // First call? Initialize to __heap_start
    bnez t1, sbrk_initialized

    // Initialize heap break to __heap_start
7:  auipc t1, %pcrel_hi(__heap_start)
    addi t1, t1, %pcrel_lo(7b)

sbrk_initialized:
    // Calculate new break
    add t2, t1, a0              // new_break = current_break + increment

    // Check if new break exceeds __heap_end
8:  auipc t3, %pcrel_hi(__heap_end)
    addi t3, t3, %pcrel_lo(8b)

    bltu t2, t3, sbrk_ok        // if new_break < heap_end, OK

    // Out of memory
    li a0, -1
    ret

sbrk_ok:
    // Update heap break
    sw t2, 0(t0)

    // Return old break
    mv a0, t1
    ret

.option pop

//==============================================================================
// Data Section
//==============================================================================

.section .bss
.align 4

// Heap break pointer (current end of allocated heap)
_heap_break:
    .word 0

//==============================================================================
// Notes
//==============================================================================

/*
 * Position-Independent Code (PIC) Patterns:
 *
 * 1. Loading address into register:
 *    WRONG:  la t0, symbol         # Absolute addressing
 *    RIGHT:  auipc t0, %pcrel_hi(symbol)
 *            addi t0, t0, %pcrel_lo(label)
 *
 * 2. Calling function:
 *    WRONG:  call function          # May use absolute addressing
 *    RIGHT:  auipc ra, %pcrel_hi(function)
 *            jalr ra, %pcrel_lo(label)(ra)
 *
 * 3. Fixed addresses (OK to use absolute):
 *    - Stack pointer (0x7A000) - fixed by design
 *    - MMIO addresses (0x80000000+) - hardware mapped
 *
 * 4. Always use .option norelax around PC-relative code:
 *    - Prevents linker from "optimizing" to absolute addressing
 *    - Critical for PIC to work correctly
 *
 * Return Mechanism:
 *
 * Overlay call chain:
 *   SD Card Manager → overlay_execute(0x60000)
 *                  → _start (disables interrupts)
 *                  → main() (may re-enable for timer, etc.)
 *                  → return or exit(0)
 *                  → _exit
 *                  → ret (return to SD Card Manager)
 *
 * Interrupt Handling:
 *
 * The overlay_loader enables ALL interrupts (mask=0) before calling overlay.
 * overlay_start.S MUST disable interrupts immediately on entry to prevent
 * timer interrupts from firing before the overlay has properly initialized.
 *
 * Overlays that need interrupts (e.g., mandelbrot with timer):
 *   1. Register IRQ handler: *((void(**)(void))0x2A000) = handler;
 *   2. Initialize hardware: timer_ms_init();  (re-enables interrupts)
 *   3. Run with interrupts enabled
 *   4. Cleanup: Unregister handler, disable timer
 *
 * overlay_execute() in sd_fatfs/overlay_loader.c will disable interrupts
 * again after the overlay returns.
 */
