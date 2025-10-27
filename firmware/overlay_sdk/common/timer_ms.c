//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// timer_ms.c - Millisecond Timer Library Implementation
//
// Provides accurate millisecond timing using hardware timer interrupt
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "timer_ms.h"
#include <stdint.h>

// Timer peripheral registers (base 0x80000020)
#define TIMER_BASE          0x80000020
#define TIMER_CR            (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR            (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC           (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR           (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT           (*(volatile uint32_t*)(TIMER_BASE + 0x10))

// Timer control bits
#define TIMER_CR_ENABLE     (1 << 0)
#define TIMER_CR_ONE_SHOT   (1 << 1)
#define TIMER_SR_UIF        (1 << 0)

// PicoRV32 Custom IRQ Enable instruction
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

// Global millisecond counter (wraps every ~49 days)
volatile uint32_t millis_counter = 0;

//==============================================================================
// Initialize millisecond timer
//
// Configures timer for 1 kHz (1ms period) interrupts
// System clock: 50 MHz
// Prescaler: 49 (divide by 50) → 1 MHz tick rate
// Auto-reload: 999 → 1,000,000 / 1000 = 1000 Hz = 1ms period
//==============================================================================
void timer_ms_init(void) {
    // Disable timer
    TIMER_CR = 0;

    // Clear any pending interrupt
    TIMER_SR = TIMER_SR_UIF;

    // Configure for 1 kHz (1ms)
    TIMER_PSC = 49;   // Prescaler: 50 MHz / 50 = 1 MHz
    TIMER_ARR = 999;  // Auto-reload: 1 MHz / 1000 = 1 kHz

    // Reset counter
    millis_counter = 0;

    // Enable interrupts
    irq_enable();

    // Start timer in continuous mode
    TIMER_CR = TIMER_CR_ENABLE;
}

//==============================================================================
// Timer interrupt handler (called from main irq_handler)
//==============================================================================
void timer_ms_irq_handler(void) {
    // Clear interrupt flag
    TIMER_SR = TIMER_SR_UIF;

    // Increment millisecond counter
    millis_counter++;
}

//==============================================================================
// Get current millisecond count
//==============================================================================
uint32_t get_millis(void) {
    return millis_counter;
}

//==============================================================================
// Sleep for specified milliseconds (blocking)
//==============================================================================
void sleep_milli(int milliseconds) {
    uint32_t start = millis_counter;
    uint32_t target = start + milliseconds;

    // Handle counter wrap-around
    if (target < start) {
        // Wait for wrap
        while (millis_counter >= start);
    }

    // Wait until target reached
    while (millis_counter < target);
}
