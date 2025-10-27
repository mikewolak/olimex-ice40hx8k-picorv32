//==============================================================================
// Crash Dump Module - Implementation
//==============================================================================

#include "crash_dump.h"
#include "hardware.h"
#include <stdio.h>

// Global crash context - filled by assembly IRQ wrapper in start.S
crash_context_t g_crash_context;

static volatile uint32_t watchdog_enabled = 0;

//==============================================================================
// Watchdog Control
//==============================================================================

void crash_watchdog_enable(uint32_t timeout_ms) {
    // Configure timer for one-shot interrupt
    TIMER_CTRL = 0;  // Disable first
    TIMER_COMPARE = TIMER_MS_TO_TICKS(timeout_ms);
    TIMER_STATUS = TIMER_SR_UIF;  // Clear any pending interrupt
    TIMER_CTRL = TIMER_ENABLE | TIMER_IRQ_ENABLE | TIMER_ONE_SHOT;

    watchdog_enabled = 1;

    printf("Watchdog enabled: %lu ms timeout\r\n", (unsigned long)timeout_ms);
}

void crash_watchdog_disable(void) {
    TIMER_CTRL = 0;  // Disable timer
    TIMER_STATUS = TIMER_SR_UIF;  // Clear interrupt
    watchdog_enabled = 0;

    printf("Watchdog disabled\r\n");
}

void crash_watchdog_pet(void) {
    if (watchdog_enabled) {
        // Restart timer
        TIMER_CTRL = 0;  // Disable
        TIMER_STATUS = TIMER_SR_UIF;  // Clear flag
        TIMER_CTRL = TIMER_ENABLE | TIMER_IRQ_ENABLE | TIMER_ONE_SHOT;
    }
}

//==============================================================================
// Crash Dump Functions
//==============================================================================

void crash_dump_context(const crash_context_t *ctx) {
    printf("\r\n");
    printf("================================================================================\r\n");
    printf("                          CRASH DUMP - OVERLAY HUNG\r\n");
    printf("================================================================================\r\n");
    printf("\r\n");

    printf("Program Counter: 0x%08lX\r\n", (unsigned long)ctx->pc);
    printf("IRQ Mask:        0x%08lX\r\n", (unsigned long)ctx->irq_mask);
    printf("\r\n");

    printf("Integer Registers:\r\n");
    printf("  ra  (x1):  0x%08lX    sp  (x2):  0x%08lX\r\n",
           (unsigned long)ctx->ra, (unsigned long)ctx->sp);
    printf("  gp  (x3):  0x%08lX    tp  (x4):  0x%08lX\r\n",
           (unsigned long)ctx->gp, (unsigned long)ctx->tp);
    printf("  t0  (x5):  0x%08lX    t1  (x6):  0x%08lX\r\n",
           (unsigned long)ctx->t0, (unsigned long)ctx->t1);
    printf("  t2  (x7):  0x%08lX    s0  (x8):  0x%08lX\r\n",
           (unsigned long)ctx->t2, (unsigned long)ctx->s0);
    printf("  s1  (x9):  0x%08lX\r\n", (unsigned long)ctx->s1);
    printf("\r\n");

    printf("Function Arguments / Return Values:\r\n");
    printf("  a0 (x10):  0x%08lX    a1 (x11):  0x%08lX\r\n",
           (unsigned long)ctx->a0, (unsigned long)ctx->a1);
    printf("  a2 (x12):  0x%08lX    a3 (x13):  0x%08lX\r\n",
           (unsigned long)ctx->a2, (unsigned long)ctx->a3);
    printf("  a4 (x14):  0x%08lX    a5 (x15):  0x%08lX\r\n",
           (unsigned long)ctx->a4, (unsigned long)ctx->a5);
    printf("  a6 (x16):  0x%08lX    a7 (x17):  0x%08lX\r\n",
           (unsigned long)ctx->a6, (unsigned long)ctx->a7);
    printf("\r\n");

    printf("Saved Registers:\r\n");
    printf("  s2 (x18):  0x%08lX    s3 (x19):  0x%08lX\r\n",
           (unsigned long)ctx->s2, (unsigned long)ctx->s3);
    printf("  s4 (x20):  0x%08lX    s5 (x21):  0x%08lX\r\n",
           (unsigned long)ctx->s4, (unsigned long)ctx->s5);
    printf("  s6 (x22):  0x%08lX    s7 (x23):  0x%08lX\r\n",
           (unsigned long)ctx->s6, (unsigned long)ctx->s7);
    printf("  s8 (x24):  0x%08lX    s9 (x25):  0x%08lX\r\n",
           (unsigned long)ctx->s8, (unsigned long)ctx->s9);
    printf("  s10(x26):  0x%08lX    s11(x27):  0x%08lX\r\n",
           (unsigned long)ctx->s10, (unsigned long)ctx->s11);
    printf("\r\n");

    printf("Temporaries:\r\n");
    printf("  t3 (x28):  0x%08lX    t4 (x29):  0x%08lX\r\n",
           (unsigned long)ctx->t3, (unsigned long)ctx->t4);
    printf("  t5 (x30):  0x%08lX    t6 (x31):  0x%08lX\r\n",
           (unsigned long)ctx->t5, (unsigned long)ctx->t6);
    printf("\r\n");
}

void crash_dump_memory(uint32_t addr, uint32_t size) {
    printf("Memory Dump: 0x%08lX - 0x%08lX (%lu bytes)\r\n",
           (unsigned long)addr,
           (unsigned long)(addr + size - 1),
           (unsigned long)size);
    printf("\r\n");

    for (uint32_t i = 0; i < size; i += 16) {
        printf("  %08lX: ", (unsigned long)(addr + i));

        // Print hex
        for (uint32_t j = 0; j < 16 && (i + j) < size; j++) {
            uint8_t byte = *((volatile uint8_t*)(addr + i + j));
            printf("%02X ", byte);
        }

        // Padding
        for (uint32_t j = (size - i); j < 16; j++) {
            printf("   ");
        }

        printf(" |");

        // Print ASCII
        for (uint32_t j = 0; j < 16 && (i + j) < size; j++) {
            uint8_t byte = *((volatile uint8_t*)(addr + i + j));
            if (byte >= 32 && byte < 127) {
                printf("%c", byte);
            } else {
                printf(".");
            }
        }

        printf("|\r\n");
    }
    printf("\r\n");
}

void crash_dump_stack(uint32_t sp, uint32_t depth) {
    printf("Stack Dump (SP = 0x%08lX, depth = %lu words):\r\n",
           (unsigned long)sp, (unsigned long)depth);
    printf("\r\n");

    for (uint32_t i = 0; i < depth; i++) {
        uint32_t addr = sp + (i * 4);
        uint32_t value = *((volatile uint32_t*)addr);
        printf("  [SP+%3lu] 0x%08lX: 0x%08lX\r\n",
               (unsigned long)(i * 4),
               (unsigned long)addr,
               (unsigned long)value);
    }
    printf("\r\n");
}
