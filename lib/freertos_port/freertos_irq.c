/*
 * FreeRTOS Timer Tick Integration for PicoRV32
 *
 * Provides timer interrupt handler for FreeRTOS tick generation.
 * Uses PicoRV32 timer peripheral at 0x80000020.
 *
 * Copyright (c) October 2025 Michael Wolak
 * Email: mikewolak@gmail.com, mike@epromfoundry.com
 */

#include <FreeRTOS.h>
#include <task.h>
#include <stdint.h>

//==============================================================================
// Timer Peripheral Registers (Base: 0x80000020)
//==============================================================================

#define TIMER_BASE          0x80000020
#define TIMER_CR            (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR            (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC           (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR           (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT           (*(volatile uint32_t*)(TIMER_BASE + 0x10))

// Timer control register bits
#define TIMER_CR_ENABLE     (1 << 0)    // Timer enable
#define TIMER_CR_ONE_SHOT   (1 << 1)    // One-shot mode (0 = continuous)

// Timer status register bits
#define TIMER_SR_UIF        (1 << 0)    // Update interrupt flag

//==============================================================================
// Timer Tick Configuration
//==============================================================================

/*
 * Timer tick calculation for FreeRTOS:
 *
 * Target: configTICK_RATE_HZ (1000 Hz = 1 ms tick)
 * System clock: configCPU_CLOCK_HZ (50,000,000 Hz = 50 MHz)
 *
 * Timer operation:
 *   - Prescaler (PSC) divides the system clock
 *   - Counter (CNT) counts from 0 to Auto-Reload Register (ARR)
 *   - Interrupt fires when CNT reaches ARR
 *
 * Formula:
 *   Interrupt frequency = CPU_CLOCK / (PSC + 1) / (ARR + 1)
 *
 * For 1 KHz (1 ms tick):
 *   PSC = 49  → Clock after prescaler = 50MHz / 50 = 1 MHz
 *   ARR = 999 → Interrupt rate = 1MHz / 1000 = 1 KHz
 *
 * Note: PSC and ARR are 0-indexed (PSC=49 means divide by 50)
 * Verified: timer_clock.c uses PSC=49, ARR=16666 for perfect 60 Hz
 */

#define TIMER_PRESCALER     49      // 50 MHz / 50 = 1 MHz
#define TIMER_AUTO_RELOAD   999     // 1 MHz / 1000 = 1 KHz (1 ms)

//==============================================================================
// Timer Initialization
//==============================================================================

/*
 * Initialize timer for FreeRTOS tick generation.
 * Called by xPortStartScheduler() before enabling interrupts.
 */
void vPortSetupTimerInterrupt(void)
{
    // Disable timer during configuration
    TIMER_CR = 0;

    // Clear any pending interrupt flag
    TIMER_SR = TIMER_SR_UIF;

    // Configure prescaler for 1 MHz timer clock
    TIMER_PSC = TIMER_PRESCALER;

    // Configure auto-reload for 1 KHz tick (1 ms period)
    TIMER_ARR = TIMER_AUTO_RELOAD;

    // Reset counter to 0
    TIMER_CNT = 0;

    // Enable timer in continuous mode (interrupts enabled automatically)
    TIMER_CR = TIMER_CR_ENABLE;

    // Timer is now running and will generate interrupts at 1 KHz
}

//==============================================================================
// Interrupt Handler
//==============================================================================

// Diagnostic counter to verify timer is firing at correct rate
volatile uint32_t timer_irq_count = 0;

/*
 * IRQ Handler - overrides weak symbol from start.S
 *
 * Called by assembly IRQ vector (irq_vec) in start.S when interrupt fires.
 *
 * PicoRV32 IRQ handling:
 *   - IRQ vector saves all caller-saved registers
 *   - Reads IRQ status with getq instruction
 *   - Calls this C function with IRQ bitmask
 *   - We clear the interrupt source
 *   - IRQ vector restores registers and uses retirq to return
 *
 * Parameters:
 *   irqs - Bitmask of pending IRQs (bit 0 = timer interrupt)
 *
 * CRITICAL: Must clear interrupt flag BEFORE calling FreeRTOS functions,
 *           otherwise interrupt will re-trigger immediately!
 */
void irq_handler(uint32_t irqs)
{
    // Check if timer interrupt (IRQ bit 0)
    if (irqs & (1 << 0)) {
        // CRITICAL: Clear timer interrupt flag FIRST
        // This prevents the interrupt from re-triggering
        TIMER_SR = TIMER_SR_UIF;

        // Diagnostic: Count timer interrupts
        timer_irq_count++;

        // Increment FreeRTOS tick counter
        // NOTE: xPortSysTickHandler() is the standard name for tick ISR in many ports
        // For PicoRV32, we call xTaskIncrementTick() directly
        // This increments xTickCount and unblocks tasks waiting for this tick
        if (xTaskIncrementTick() != pdFALSE) {
            // A context switch is required (higher priority task now ready)
            // vTaskSwitchContext() updates pxCurrentTCB to point to new task
            // When we return from IRQ, the assembly will restore from new task's stack
            vTaskSwitchContext();
        }
    }

    // Future: Handle other interrupt sources here
    // if (irqs & (1 << 1)) { ... }  // UART interrupt
    // if (irqs & (1 << 2)) { ... }  // GPIO interrupt
    // etc.
}

//==============================================================================
// Diagnostics (for debugging)
//==============================================================================

/*
 * Get current timer counter value.
 * Useful for timing measurements and debugging.
 */
uint32_t ulGetTimerCounter(void)
{
    return TIMER_CNT;
}

/*
 * Get timer frequency in Hz.
 * Should return configTICK_RATE_HZ (1000).
 */
uint32_t ulGetTimerFrequency(void)
{
    return configTICK_RATE_HZ;
}
