//==============================================================================
// Crash Dump Module - Debug Overlay Crashes
//
// Provides crash detection and register dump for debugging overlay hangs.
// Uses timer watchdog to detect when overlay stops making progress.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef CRASH_DUMP_H
#define CRASH_DUMP_H

#include <stdint.h>

//==============================================================================
// Crash Context Structure
//==============================================================================

typedef struct {
    // Saved registers from IRQ vector
    uint32_t ra;   // Return address
    uint32_t sp;   // Stack pointer
    uint32_t gp;   // Global pointer
    uint32_t tp;   // Thread pointer
    uint32_t t0;   // Temporaries
    uint32_t t1;
    uint32_t t2;
    uint32_t s0;   // Saved registers
    uint32_t s1;
    uint32_t a0;   // Arguments/return values
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s2;   // More saved registers
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t t3;   // More temporaries
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;

    // PC is stored in q2 by PicoRV32
    uint32_t pc;   // Program counter (from q2)

    // IRQ info
    uint32_t irq_mask;  // From q1
} crash_context_t;

//==============================================================================
// Function Prototypes
//==============================================================================

// Enable watchdog with timeout (in milliseconds)
void crash_watchdog_enable(uint32_t timeout_ms);

// Disable watchdog
void crash_watchdog_disable(void);

// Pet the watchdog (reset timer)
void crash_watchdog_pet(void);

// Dump crash context to UART
void crash_dump_context(const crash_context_t *ctx);

// Dump memory region
void crash_dump_memory(uint32_t addr, uint32_t size);

// Dump stack trace
void crash_dump_stack(uint32_t sp, uint32_t depth);

#endif // CRASH_DUMP_H
