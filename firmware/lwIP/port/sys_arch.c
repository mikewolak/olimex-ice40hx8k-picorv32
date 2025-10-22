/*
 * System Architecture Layer for lwIP NO_SYS mode
 *
 * Provides timing functions for lwIP timeouts
 * Uses PicoRV32 timer peripheral for tick count
 *
 * Copyright (c) October 2025 Michael Wolak
 */

#include "lwip/opt.h"
#include "lwip/sys.h"
#include <stdint.h>

/*
 * Timer Peripheral Registers
 * Base: 0x80000020
 */
#define TIMER_BASE      0x80000020
#define TIMER_CR        (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR        (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC       (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR       (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT       (*(volatile uint32_t*)(TIMER_BASE + 0x10))

#define TIMER_CR_ENABLE (1 << 0)

/*
 * Initialize timer for 1 KHz tick (1 ms resolution)
 *
 * System clock: 50 MHz
 * Prescaler: 49 (divide by 50) → 1 MHz
 * Auto-reload: 999 (count 0-999) → 1 KHz tick
 */
void sys_init_timing(void)
{
    TIMER_CR = 0;          /* Disable timer */
    TIMER_PSC = 49;        /* Prescaler: 50MHz / 50 = 1MHz */
    TIMER_ARR = 999;       /* Auto-reload: 1MHz / 1000 = 1KHz (1ms) */
    TIMER_CNT = 0;         /* Reset counter */
    TIMER_CR = TIMER_CR_ENABLE;  /* Enable timer */
}

/*
 * sys_now - Get current time in milliseconds
 *
 * Returns milliseconds since sys_init_timing() was called
 * Used by lwIP for timeout management
 */
u32_t sys_now(void)
{
    /* Timer counts 0-999 at 1 KHz, so we need to track milliseconds */
    static u32_t ms_count = 0;
    static u32_t last_counter = 0;

    u32_t current = TIMER_CNT;

    /* Check for timer rollover (counter went back to 0) */
    if (current < last_counter) {
        ms_count++;  /* Increment millisecond count on rollover */
    }

    last_counter = current;

    return ms_count;
}

/*
 * sys_init - Initialize sys layer
 *
 * Called by lwip_init()
 */
void sys_init(void)
{
    sys_init_timing();
}

/*
 * Critical Section Protection (NO_SYS mode)
 *
 * In NO_SYS mode, these provide thread-safety by disabling interrupts
 * Uses PicoRV32 custom 'maskirq' instruction
 */

/* Disable all interrupts and return previous interrupt state */
sys_prot_t sys_arch_protect(void)
{
    sys_prot_t old_mask;

    /* Read current interrupt mask and then disable all interrupts */
    __asm__ volatile (
        ".insn r 0x0B, 6, 3, %0, %1, x0"  /* maskirq rd, rs1 */
        : "=r"(old_mask)                   /* output: old mask */
        : "r"(~0u)                          /* input: new mask (disable all) */
    );

    return old_mask;
}

/* Restore previous interrupt state */
void sys_arch_unprotect(sys_prot_t pval)
{
    uint32_t dummy;

    /* Restore previous interrupt mask */
    __asm__ volatile (
        ".insn r 0x0B, 6, 3, %0, %1, x0"  /* maskirq rd, rs1 */
        : "=r"(dummy)                      /* output: discarded */
        : "r"(pval)                        /* input: mask to restore */
    );
}
