// RISC-V Startup Code with FreeRTOS Context Switching Support
// Based on start.S with modifications for task switching

.section .text.start
.global _start

_start:
    /* Jump over IRQ vector to initialization code */
    j init_start

//==============================================================================
// Interrupt Vector with FreeRTOS Context Switching (PROGADDR_IRQ = 0x10)
//==============================================================================

.balign 16
.global irq_vec
irq_vec:
    /* Save ALL caller-saved registers to current task's stack */
    addi sp, sp, -64
    sw ra,  0(sp)
    sw a0,  4(sp)
    sw a1,  8(sp)
    sw a2, 12(sp)
    sw a3, 16(sp)
    sw a4, 20(sp)
    sw a5, 24(sp)
    sw a6, 28(sp)
    sw a7, 32(sp)
    sw t0, 36(sp)
    sw t1, 40(sp)
    sw t2, 44(sp)
    sw t3, 48(sp)
    sw t4, 52(sp)
    sw t5, 56(sp)
    sw t6, 60(sp)

    /* Save current task's stack pointer to TCB
     * FreeRTOS maintains pxCurrentTCB which points to current task's TCB
     * TCB structure: first field is void *pxTopOfStack
     * So we need to: *pxCurrentTCB = sp
     */
    la t0, pxCurrentTCB     // Load address of pxCurrentTCB pointer
    lw t0, 0(t0)            // Load pxCurrentTCB (pointer to current TCB)
    sw sp, 0(t0)            // Save sp to TCB->pxTopOfStack (first field)

    /* Read which IRQ(s) fired from q1 */
    .insn r 0x0B, 4, 0, a0, x1, x0  // getq a0, q1

    /* Call C interrupt handler
     * This may call vTaskSwitchContext() which changes pxCurrentTCB
     */
    call irq_handler

    /* Load (possibly new) task's stack pointer from TCB
     * If vTaskSwitchContext() was called, pxCurrentTCB now points to
     * a different task, so we load that task's stack pointer
     */
    la t0, pxCurrentTCB     // Load address of pxCurrentTCB pointer
    lw t0, 0(t0)            // Load pxCurrentTCB (might be different now!)
    lw sp, 0(t0)            // Load new sp from TCB->pxTopOfStack

    /* Restore ALL caller-saved registers from (possibly new) task's stack */
    lw ra,  0(sp)
    lw a0,  4(sp)
    lw a1,  8(sp)
    lw a2, 12(sp)
    lw a3, 16(sp)
    lw a4, 20(sp)
    lw a5, 24(sp)
    lw a6, 28(sp)
    lw a7, 32(sp)
    lw t0, 36(sp)
    lw t1, 40(sp)
    lw t2, 44(sp)
    lw t3, 48(sp)
    lw t4, 52(sp)
    lw t5, 56(sp)
    lw t6, 60(sp)
    addi sp, sp, 64

    /* Return from interrupt (to possibly different task!) */
    .insn r 0x0B, 0, 2, x0, x0, x0  // retirq

//==============================================================================
// Initialization Code
//==============================================================================

init_start:
    /* Set up stack pointer */
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

    /* Set up argc and argv for main(int argc, char **argv) */
    li a0, 0        // argc = 0
    li a1, 0        // argv = NULL

    /* Call main function */
    call main

    /* Infinite loop if main returns */
loop_forever:
    j loop_forever

//==============================================================================
// FreeRTOS: Start First Task
//==============================================================================

/*
 * vPortStartFirstTask - Jump to first task without returning
 *
 * Called by xPortStartScheduler() after:
 *   1. Timer initialized
 *   2. pxCurrentTCB points to first task
 *   3. First task's stack initialized by pxPortInitialiseStack()
 *
 * This function:
 *   1. Loads first task's SP from pxCurrentTCB->pxTopOfStack
 *   2. Restores all 16 caller-saved registers from task stack
 *   3. Uses retirq to "return" to task (ra = task entry point)
 *
 * NEVER RETURNS - this is the point of no return for the scheduler!
 */
.global vPortStartFirstTask
vPortStartFirstTask:
    // DEBUG: Print message before loading pxCurrentTCB
    call debug_asm_msg1

    /* Load pxCurrentTCB (pointer to first task's TCB)
     * NOTE: Must reload after debug call since debug functions clobber t0
     */
    la t0, pxCurrentTCB     // t0 = &pxCurrentTCB

    // DEBUG: Print character 'A' to confirm we got past la instruction
    call debug_char_a

    lw t1, 0(t0)            // t1 = pxCurrentTCB (pointer to TCB) - use t1 not t0!

    // DEBUG: Print character 'B' to confirm we got past lw instruction
    call debug_char_b

    // DEBUG: Check if pxCurrentTCB is NULL
    beqz t1, error_null_tcb

    // DEBUG: Print message after loading pxCurrentTCB
    call debug_asm_msg2

    /* Load task's stack pointer from TCB->pxTopOfStack (first field)
     * pxCurrentTCB is in t1, load SP from it
     */
    lw sp, 0(t1)            // sp = pxCurrentTCB->pxTopOfStack

    // DEBUG: Check if SP is NULL
    beqz sp, error_null_sp

    // DEBUG: Print message after loading SP
    call debug_asm_msg3

    // Reload task pointer into t1 after debug call
    la t0, pxCurrentTCB
    lw t1, 0(t0)

    /* Restore ALL caller-saved registers from task's stack
     * This is IDENTICAL to the restore sequence in irq_vec
     * Stack was initialized by pxPortInitialiseStack():
     *   offset 0:  ra = task entry point
     *   offset 4:  a0 = task parameter
     *   offset 8+: other registers (zeroed)
     */
    lw t0,  0(sp)           // Load task entry point into t0 (not ra yet!)
    lw a0,  4(sp)
    lw a1,  8(sp)
    lw a2, 12(sp)
    lw a3, 16(sp)
    lw a4, 20(sp)
    lw a5, 24(sp)
    lw a6, 28(sp)
    lw a7, 32(sp)
    lw t1, 40(sp)
    lw t2, 44(sp)
    lw t3, 48(sp)
    lw t4, 52(sp)
    lw t5, 56(sp)
    lw t6, 60(sp)
    addi sp, sp, 64

    /* Set up return address to catch tasks that accidentally return
     * If task returns, it will call prvTaskExitError which loops forever
     */
    la ra, prvTaskExitError

    /* Jump to first task
     * Interrupts are already enabled by xPortStartScheduler()
     * t0 contains task entry point, ra contains error handler
     * If task returns, it jumps to prvTaskExitError
     */
    jr t0  // Jump to task entry point in t0

//==============================================================================
// Task Exit Error Handler
//==============================================================================

/*
 * prvTaskExitError - Called if a task function returns
 *
 * FreeRTOS tasks should NEVER return - they must either:
 *   1. Run in infinite loop, OR
 *   2. Call vTaskDelete(NULL) to delete themselves
 *
 * If a task returns here, it's a programming error.
 * We loop forever to make the bug obvious.
 */
prvTaskExitError:
    // Infinite loop - task should never return!
    j prvTaskExitError

error_null_tcb:
    la a0, error_msg_null_tcb
    call debug_print
1:  j 1b

error_null_sp:
    la a0, error_msg_null_sp
    call debug_print
1:  j 1b

//==============================================================================
// Debug Functions (for assembly debugging)
//==============================================================================

#define UART_TX_DATA   0x80000000
#define UART_TX_STATUS 0x80000004

// Use static storage instead of stack (stack might be invalid)
.data
.align 4
debug_save_ra: .word 0
debug_save_a0: .word 0
debug_save_a1: .word 0
debug_save_a2: .word 0
debug_save_t0: .word 0

.text
debug_asm_msg1:
    la t0, debug_save_ra
    sw ra, 0(t0)
    la t0, debug_save_a0
    sw a0, 0(t0)
    la t0, debug_save_a1
    sw a1, 0(t0)
    la t0, debug_save_a2
    sw a2, 0(t0)

    la a0, msg1
    call debug_print

    la t0, debug_save_a2
    lw a2, 0(t0)
    la t0, debug_save_a1
    lw a1, 0(t0)
    la t0, debug_save_a0
    lw a0, 0(t0)
    la t0, debug_save_ra
    lw ra, 0(t0)
    ret

debug_asm_msg2:
    la t0, debug_save_ra
    sw ra, 0(t0)
    la t0, debug_save_a0
    sw a0, 0(t0)
    la t0, debug_save_a1
    sw a1, 0(t0)
    la t0, debug_save_a2
    sw a2, 0(t0)

    la a0, msg2
    call debug_print

    la t0, debug_save_a2
    lw a2, 0(t0)
    la t0, debug_save_a1
    lw a1, 0(t0)
    la t0, debug_save_a0
    lw a0, 0(t0)
    la t0, debug_save_ra
    lw ra, 0(t0)
    ret

debug_asm_msg3:
    la t0, debug_save_ra
    sw ra, 0(t0)
    la t0, debug_save_a0
    sw a0, 0(t0)
    la t0, debug_save_a1
    sw a1, 0(t0)
    la t0, debug_save_a2
    sw a2, 0(t0)

    la a0, msg3
    call debug_print

    la t0, debug_save_a2
    lw a2, 0(t0)
    la t0, debug_save_a1
    lw a1, 0(t0)
    la t0, debug_save_a0
    lw a0, 0(t0)
    la t0, debug_save_ra
    lw ra, 0(t0)
    ret

debug_print:
    mv a1, a0
1:  lb a0, 0(a1)
    beqz a0, 2f

    la t0, debug_save_t0
    sw a1, 0(t0)
    call debug_putc
    la t0, debug_save_t0
    lw a1, 0(t0)

    addi a1, a1, 1
    j 1b
2:  ret

debug_putc:
    li a1, UART_TX_STATUS
1:  lw a2, 0(a1)
    andi a2, a2, 1
    bnez a2, 1b
    li a1, UART_TX_DATA
    sw a0, 0(a1)
    ret

// Quick debug functions that print single characters
debug_char_a:
    la t0, debug_save_ra
    sw ra, 0(t0)
    la t0, debug_save_a0
    sw a0, 0(t0)
    li a0, 'A'
    call debug_putc
    la t0, debug_save_a0
    lw a0, 0(t0)
    la t0, debug_save_ra
    lw ra, 0(t0)
    ret

debug_char_b:
    la t0, debug_save_ra
    sw ra, 0(t0)
    la t0, debug_save_a0
    sw a0, 0(t0)
    li a0, 'B'
    call debug_putc
    la t0, debug_save_a0
    lw a0, 0(t0)
    la t0, debug_save_ra
    lw ra, 0(t0)
    ret

msg1: .string "DEBUG ASM: Loading pxCurrentTCB\r\n"
msg2: .string "DEBUG ASM: Loaded pxCurrentTCB, loading SP\r\n"
msg3: .string "DEBUG ASM: Loaded SP, restoring registers\r\n"
error_msg_null_tcb: .string "ERROR: pxCurrentTCB is NULL!\r\n"
error_msg_null_sp: .string "ERROR: Task stack pointer is NULL!\r\n"

//==============================================================================
// Default (Weak) IRQ Handler
//==============================================================================

.weak irq_handler
irq_handler:
    ret  // Do nothing, just return
