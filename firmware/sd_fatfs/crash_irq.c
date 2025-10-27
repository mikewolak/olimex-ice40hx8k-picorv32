//==============================================================================
// Crash IRQ Handler - Captures Register State on Watchdog Timeout
//
// This IRQ handler is called when the watchdog timer fires, indicating
// that the overlay has hung. It captures the full register state and
// dumps it to UART for debugging.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "crash_dump.h"
#include "hardware.h"
#include <stdio.h>

// Global crash context - filled by assembly IRQ wrapper
crash_context_t g_crash_context;

//==============================================================================
// C IRQ Handler - Called from assembly wrapper with saved registers
//==============================================================================

void irq_handler(uint32_t irq_mask) {
    // Check if timer interrupt
    uint32_t timer_status = TIMER_STATUS;

    if (timer_status & TIMER_SR_UIF) {
        // Clear timer interrupt
        TIMER_STATUS = TIMER_SR_UIF;
        TIMER_CTRL = 0;  // Disable timer

        printf("\r\n");
        printf("================================================================================\r\n");
        printf("                  WATCHDOG TIMEOUT - OVERLAY HUNG!\r\n");
        printf("================================================================================\r\n");
        printf("\r\n");

        // Store IRQ mask
        g_crash_context.irq_mask = irq_mask;

        // Read PC from q2
        uint32_t pc;
        __asm__ volatile (".insn r 0x0B, 4, 0, %0, x2, x0" : "=r"(pc));
        g_crash_context.pc = pc;

        // Dump crash context (registers were saved by irq_vec_crash below)
        crash_dump_context(&g_crash_context);

        // Dump code at PC
        printf("Code at crash PC (0x%08lX):\r\n", (unsigned long)pc);
        crash_dump_memory(pc & ~0xF, 64);  // Align to 16 bytes, dump 64 bytes

        // Dump overlay entry point
        printf("Overlay entry point (0x00060000):\r\n");
        crash_dump_memory(0x00060000, 64);

        // Dump stack
        printf("Stack dump:\r\n");
        crash_dump_stack(g_crash_context.sp, 16);

        printf("================================================================================\r\n");
        printf("System halted. Reset required.\r\n");
        printf("================================================================================\r\n");

        // Halt
        while (1) {
            LED_REG = 0x03;  // Both LEDs on = crashed
        }
    }
}
