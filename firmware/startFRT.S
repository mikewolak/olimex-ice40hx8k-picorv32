// RISC-V Startup Code with FreeRTOS Context Switching Support
// Based on start.S with minimal modifications for task switching

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

    /* FreeRTOS: Save current task's stack pointer to TCB
     * pxCurrentTCB points to current task's TCB
     * TCB structure: first field is void *pxTopOfStack
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

    /* FreeRTOS: Load (possibly new) task's stack pointer from TCB
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
 * This function simulates an interrupt return into the first task.
 */
.global vPortStartFirstTask
vPortStartFirstTask:
    /* DEBUG: Print 'A' */
    li a0, 'A'
    call debug_putc

    /* Load pxCurrentTCB (pointer to first task's TCB) */
    la t0, pxCurrentTCB     // t0 = &pxCurrentTCB
    lw t0, 0(t0)            // t0 = pxCurrentTCB (pointer to TCB)

    /* DEBUG: Print 'B' */
    li a0, 'B'
    call debug_putc

    /* Load task's stack pointer from TCB->pxTopOfStack (first field) */
    lw sp, 0(t0)            // sp = pxCurrentTCB->pxTopOfStack

    /* DEBUG: Print 'C' */
    li a0, 'C'
    call debug_putc

    /* Load task entry point and parameter, adjust SP
     * Stack was initialized by pxPortInitialiseStack():
     *   offset 0:  ra = task entry point
     *   offset 4:  a0 = task parameter
     *   offset 8+: other registers (zeroed)
     */
    lw t0,  0(sp)            // t0 = task entry point
    lw a0,  4(sp)            // a0 = task parameter
    addi sp, sp, 64          // Pop the initial stack frame

    /* DEBUG: Print 'D' */
    mv t1, a0                // Save a0
    li a0, 'D'
    call debug_putc
    mv a0, t1                // Restore a0

    /* Jump to task
     * Interrupts are already enabled by xPortStartScheduler()
     * Just jump directly to the task entry point
     */
    jr t0

//==============================================================================
// Debug helper function
//==============================================================================
debug_putc:
    li t2, 0x80000004        // UART_TX_STATUS
1:  lw t3, 0(t2)
    andi t3, t3, 1
    bnez t3, 1b
    li t2, 0x80000000        // UART_TX_DATA
    sw a0, 0(t2)
    ret

//==============================================================================
// Default (Weak) IRQ Handler
//==============================================================================

.weak irq_handler
irq_handler:
    ret  // Do nothing, just return
