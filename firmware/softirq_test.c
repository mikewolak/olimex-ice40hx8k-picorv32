/*
 * Software IRQ / Trap Test
 * Tests the software interrupt peripheral without FreeRTOS
 */

#include <stdint.h>

// Hardware
#define UART_TX_DATA    (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS  (*(volatile uint32_t*)0x80000004)

// Software IRQ peripheral (moved from 0x80000030 to avoid collision with TIMER_CNT)
#define SOFT_IRQ_TRIGGER  (*(volatile uint32_t*)0x80000040)
#define SOFT_IRQ_TYPE     (*(volatile uint32_t*)0x80000044)

// Trigger types
#define TYPE_YIELD      0
#define TYPE_SYSCALL    1
#define TYPE_BREAKPOINT 2
#define TYPE_TRAP       3

// Counters
volatile uint32_t timer_irq_count = 0;
volatile uint32_t soft_irq_count = 0;
volatile uint32_t last_soft_irq_type = 0xFFFFFFFF;

// Enable IRQ in PicoRV32
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

void uart_putc(char c) {
    while (UART_TX_STATUS & 1);
    UART_TX_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

void uart_print_hex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

// IRQ handler
void irq_handler(uint32_t irqs) {
    // Timer IRQ
    if (irqs & (1 << 0)) {
        timer_irq_count++;
    }

    // Software IRQ
    if (irqs & (1 << 1)) {
        soft_irq_count++;
        last_soft_irq_type = SOFT_IRQ_TYPE;  // Read the type register
    }
}

int main(void) {
    // Enable interrupts in CPU
    irq_enable();

    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("Software IRQ / Trap Test\r\n");
    uart_puts("========================================\r\n");
    uart_puts("\r\n");

    uart_puts("Testing software interrupt peripheral\r\n");
    uart_puts("Base address: 0x80000040 (TRIGGER), 0x80000044 (TYPE)\r\n\r\n");

    // Test 1: YIELD type
    uart_puts("Test 1: Trigger with TYPE_YIELD (0)\r\n");
    SOFT_IRQ_TRIGGER = TYPE_YIELD;
    uart_puts("  Soft IRQ count: ");
    uart_print_hex(soft_irq_count);
    uart_puts("\r\n");
    uart_puts("  Last type: ");
    uart_print_hex(last_soft_irq_type);
    uart_puts(" (expected 0x00000000)\r\n\r\n");

    // Test 2: SYSCALL type
    uart_puts("Test 2: Trigger with TYPE_SYSCALL (1)\r\n");
    SOFT_IRQ_TRIGGER = TYPE_SYSCALL;
    uart_puts("  Soft IRQ count: ");
    uart_print_hex(soft_irq_count);
    uart_puts("\r\n");
    uart_puts("  Last type: ");
    uart_print_hex(last_soft_irq_type);
    uart_puts(" (expected 0x00000001)\r\n\r\n");

    // Test 3: BREAKPOINT type
    uart_puts("Test 3: Trigger with TYPE_BREAKPOINT (2)\r\n");
    SOFT_IRQ_TRIGGER = TYPE_BREAKPOINT;
    uart_puts("  Soft IRQ count: ");
    uart_print_hex(soft_irq_count);
    uart_puts("\r\n");
    uart_puts("  Last type: ");
    uart_print_hex(last_soft_irq_type);
    uart_puts(" (expected 0x00000002)\r\n\r\n");

    // Test 4: TRAP type
    uart_puts("Test 4: Trigger with TYPE_TRAP (3)\r\n");
    SOFT_IRQ_TRIGGER = TYPE_TRAP;
    uart_puts("  Soft IRQ count: ");
    uart_print_hex(soft_irq_count);
    uart_puts("\r\n");
    uart_puts("  Last type: ");
    uart_print_hex(last_soft_irq_type);
    uart_puts(" (expected 0x00000003)\r\n\r\n");

    // Test 5: Custom type
    uart_puts("Test 5: Trigger with custom type (0xDEADBEEF)\r\n");
    SOFT_IRQ_TRIGGER = 0xDEADBEEF;
    uart_puts("  Soft IRQ count: ");
    uart_print_hex(soft_irq_count);
    uart_puts("\r\n");
    uart_puts("  Last type: ");
    uart_print_hex(last_soft_irq_type);
    uart_puts(" (expected 0xDEADBEEF)\r\n\r\n");

    uart_puts("========================================\r\n");
    uart_puts("All tests complete!\r\n");
    uart_puts("Expected: 5 soft IRQ triggers\r\n");
    uart_puts("Actual:   ");
    uart_print_hex(soft_irq_count);
    uart_puts("\r\n");

    if (soft_irq_count == 5) {
        uart_puts("SUCCESS: Software IRQ peripheral working!\r\n");
    } else {
        uart_puts("FAILURE: IRQ count mismatch!\r\n");
    }
    uart_puts("========================================\r\n");

    for (;;) {
        __asm__ volatile ("nop");
    }

    return 0;
}
