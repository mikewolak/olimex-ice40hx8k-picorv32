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

/* PicoRV32 IRQ enable inline function */
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

/*
 * Initialize timer for 1 KHz tick (1 ms resolution)
 *
 * System clock: 50 MHz
 * Prescaler: 49 (divide by 50) → 1 MHz
 * Auto-reload: 999 (count 0-999) → 1 KHz tick
 *
 * IMPORTANT: Matches timer_clock.c initialization sequence
 */
void sys_init_timing(void)
{
    TIMER_CR = 0;               /* Disable timer */
    TIMER_SR = 0x01;            /* Clear any pending interrupt (UIF bit) */
    TIMER_PSC = 49;             /* Prescaler: 50MHz / 50 = 1MHz */
    TIMER_ARR = 999;            /* Auto-reload: 1MHz / 1000 = 1KHz (1ms) */
    /* NOTE: Do NOT write to TIMER_CNT - it's read-only and causes lockup */

    irq_enable();               /* Enable interrupts globally */
    TIMER_CR = TIMER_CR_ENABLE; /* Start timer */
}

/*
 * Millisecond counter - incremented by timer interrupt
 * MUST be volatile because it's modified by interrupt handler
 */
static volatile u32_t ms_count = 0;

/*
 * sys_now - Get current time in milliseconds
 *
 * INTERRUPT-DRIVEN: Returns millisecond counter incremented by IRQ handler
 * Does NOT read TIMER_CNT register (which causes lockup)
 * Matches timer_clock.c pattern
 */
u32_t sys_now(void)
{
    return ms_count;
}

/*
 * sys_timer_tick - Increment millisecond counter
 *
 * Called by irq_handler() in slip_echo_server.c when timer interrupt fires
 * This is the same pattern as timer_clock.c which increments frame counter
 */
void sys_timer_tick(void)
{
    ms_count++;
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
