//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// timer_ms.h - Millisecond Timer Library Header
//
// Provides accurate millisecond timing using hardware timer interrupt
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef TIMER_MS_H
#define TIMER_MS_H

#include <stdint.h>

// Initialize millisecond timer (1 kHz interrupt rate)
void timer_ms_init(void);

// Get current millisecond count (wraps every ~49 days)
uint32_t get_millis(void);

// Sleep for specified milliseconds (blocking)
void sleep_milli(int milliseconds);

// Called by IRQ handler - do not call directly
void timer_ms_irq_handler(void);

#endif // TIMER_MS_H
