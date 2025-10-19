/*
 * FreeRTOS PicoRV32 Port
 *
 * Core port implementation using PicoRV32 custom interrupt system
 */

#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

/* PicoRV32 IRQ control (from portmacro.h) */
extern uint32_t picorv32_maskirq(uint32_t mask);

/* Critical nesting counter */
static UBaseType_t uxCriticalNesting = 0;

/*
 * Setup the stack of a new task
 */
StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack,
                                   TaskFunction_t pxCode,
                                   void *pvParameters)
{
    /* Simulate the stack frame as created by context switch
     * Stack layout (grows downward):
     *   - Task entry point in ra
     *   - Parameter in a0
     *   - All other registers zeroed
     */

    /* Leave room for 16 caller-saved registers (64 bytes total)
     * This matches what startFRT.S irq_vec saves/restores:
     * ra, a0-a7, t0-t6 */
    pxTopOfStack -= 16;

    /* Initialize all registers to zero */
    memset(pxTopOfStack, 0, 16 * sizeof(StackType_t));

    /* Set up initial register values to match stack frame layout:
     * Stack offset 0: ra (return address) = task entry point
     * Stack offset 4: a0 (argument 0) = task parameter
     * When task first runs via retirq, these will be restored
     */
    pxTopOfStack[0] = (StackType_t)pxCode;        /* ra - task entry point */
    pxTopOfStack[1] = (StackType_t)pvParameters;  /* a0 - task parameter */

    return pxTopOfStack;
}

/*
 * Start the scheduler
 */

/* Implemented in freertos_irq.c */
extern void vPortSetupTimerInterrupt(void);

/* Implemented in startFRT.S */
extern void vPortStartFirstTask(void) __attribute__((noreturn));

BaseType_t xPortStartScheduler(void)
{
    /* Initialize timer for tick generation (1 KHz = 1 ms tick) */
    vPortSetupTimerInterrupt();

    /* CRITICAL: Enable interrupts BEFORE jumping to first task
     * Timer will start generating ticks immediately
     * First task will run with interrupts enabled
     */
    picorv32_maskirq(0);

    /* Jump to first task - NEVER RETURNS!
     *
     * vPortStartFirstTask() does:
     *   1. Loads SP from pxCurrentTCB->pxTopOfStack
     *   2. Restores all 16 caller-saved registers from task stack
     *   3. Uses retirq to jump to task entry point (in ra)
     *
     * This simulates returning from an interrupt into the first task.
     * From this point forward, we're running in task context with
     * interrupts enabled and the timer tick firing every 1ms.
     */
    vPortStartFirstTask();

    /* Should NEVER reach here - vPortStartFirstTask() never returns */
    return pdFALSE;
}

/*
 * End scheduler (not typically used in embedded)
 */
void vPortEndScheduler(void)
{
    /* Disable interrupts */
    picorv32_maskirq(~0);
}

/*
 * Enter critical section
 */
void vPortEnterCritical(void)
{
    /* Disable interrupts */
    picorv32_maskirq(~0);

    /* Increment nesting count */
    uxCriticalNesting++;
}

/*
 * Exit critical section
 */
void vPortExitCritical(void)
{
    /* Decrement nesting count */
    if (uxCriticalNesting > 0) {
        uxCriticalNesting--;

        /* Re-enable interrupts if no longer nested */
        if (uxCriticalNesting == 0) {
            picorv32_maskirq(0);
        }
    }
}

/*
 * Malloc failed hook (required by config)
 */
#if (configUSE_MALLOC_FAILED_HOOK == 1)
void vApplicationMallocFailedHook(void)
{
    /* Hang on malloc failure */
    for (;;) {
        __asm__ volatile ("nop");
    }
}
#endif
