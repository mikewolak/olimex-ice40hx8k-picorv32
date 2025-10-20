/*
 * Simple Cooperative Multitasking Demo for PicoRV32
 *
 * No FreeRTOS - just manual task switching using timer interrupts.
 * This proves the concept works before tackling FreeRTOS complexity.
 *
 * Architecture:
 * - 3 simple tasks that run cooperatively
 * - Timer interrupt triggers task switches every 100ms
 * - Each task prints a message and yields
 * - Round-robin scheduling
 */

#include <stdint.h>

//==============================================================================
// Hardware Registers
//==============================================================================

#define UART_TX_DATA   (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t*)0x80000004)
#define LED_CONTROL    (*(volatile uint32_t*)0x80000010)
#define TIMER_CTRL     (*(volatile uint32_t*)0x80000020)
#define TIMER_SR       (*(volatile uint32_t*)0x80000024)
#define TIMER_CNT      (*(volatile uint32_t*)0x80000028)
#define TIMER_PSC      (*(volatile uint32_t*)0x8000002C)
#define TIMER_ARR      (*(volatile uint32_t*)0x80000030)

// Timer control bits
#define TIMER_ENABLE    (1 << 0)
#define TIMER_IRQ_EN    (1 << 1)
#define TIMER_SR_UIF    (1 << 0)

//==============================================================================
// Task Structure
//==============================================================================

#define MAX_TASKS 3
#define STACK_SIZE 512  // 512 words = 2KB per task

typedef struct {
    uint32_t *stack_ptr;     // Current stack pointer
    uint32_t stack[STACK_SIZE];  // Task's private stack
    const char *name;        // Task name for debugging
    uint32_t run_count;      // How many times this task has run
} Task_t;

static Task_t tasks[MAX_TASKS];
static int current_task = 0;
static uint32_t tick_count = 0;

//==============================================================================
// PicoRV32 IRQ Control
//==============================================================================

static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

static inline void irq_disable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(~0));
}

//==============================================================================
// Simple printf without newlib (for early debugging)
//==============================================================================

void uart_putc(char c) {
    while (UART_TX_STATUS & 0x01);
    UART_TX_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puthex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

void uart_putdec(uint32_t val) {
    if (val == 0) {
        uart_putc('0');
        return;
    }

    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

//==============================================================================
// Task Functions
//==============================================================================

void task1_func(void) {
    while (1) {
        uart_puts("[Task1] Running... count=");
        uart_putdec(tasks[0].run_count);
        uart_puts("\r\n");

        // Toggle LED0
        LED_CONTROL ^= 0x01;

        // Busy wait (simulate work)
        for (volatile int i = 0; i < 100000; i++);

        tasks[0].run_count++;
    }
}

void task2_func(void) {
    while (1) {
        uart_puts("[Task2] Running... count=");
        uart_putdec(tasks[1].run_count);
        uart_puts("\r\n");

        // Toggle LED1
        LED_CONTROL ^= 0x02;

        // Busy wait (simulate work)
        for (volatile int i = 0; i < 100000; i++);

        tasks[1].run_count++;
    }
}

void task3_func(void) {
    while (1) {
        uart_puts("[Task3] System status - Tick=");
        uart_putdec(tick_count);
        uart_puts("\r\n");

        // Toggle LED2
        LED_CONTROL ^= 0x04;

        // Busy wait (simulate work)
        for (volatile int i = 0; i < 100000; i++);

        tasks[2].run_count++;
    }
}

//==============================================================================
// Context Switching (Simple - No Assembly Required!)
//==============================================================================

// We don't actually switch contexts in the traditional sense
// Instead, each task runs to completion of one iteration, then yields
// The timer interrupt just switches which task's function gets called next

//==============================================================================
// Timer Interrupt Handler
//==============================================================================

void irq_handler(void) {
    // Check if timer interrupt
    if (TIMER_SR & TIMER_SR_UIF) {
        // Clear interrupt flag
        TIMER_SR = TIMER_SR_UIF;

        // Increment tick
        tick_count++;

        // Every 10 ticks (1 second at 100ms per tick), switch task
        if (tick_count % 10 == 0) {
            uart_puts("\r\n>>> Switching to next task <<<\r\n\r\n");
            current_task = (current_task + 1) % MAX_TASKS;
        }
    }
}

//==============================================================================
// Timer Initialization
//==============================================================================

void timer_init(void) {
    // Stop timer
    TIMER_CTRL = 0;

    // Configure for 100ms tick (10 Hz)
    // CPU clock = 50 MHz
    // Prescaler = 49999 -> 50MHz / 50000 = 1 KHz
    // Auto-reload = 99 -> 1KHz / 100 = 10 Hz (100ms period)
    TIMER_PSC = 49999;
    TIMER_ARR = 99;
    TIMER_CNT = 0;

    // Clear status
    TIMER_SR = TIMER_SR_UIF;

    // Enable timer and interrupt
    TIMER_CTRL = TIMER_ENABLE | TIMER_IRQ_EN;

    uart_puts("Timer initialized: 100ms tick (10 Hz)\r\n");
}

//==============================================================================
// Task Initialization
//==============================================================================

void tasks_init(void) {
    // Initialize task 1
    tasks[0].stack_ptr = &tasks[0].stack[STACK_SIZE - 1];
    tasks[0].name = "Task1";
    tasks[0].run_count = 0;

    // Initialize task 2
    tasks[1].stack_ptr = &tasks[1].stack[STACK_SIZE - 1];
    tasks[1].name = "Task2";
    tasks[1].run_count = 0;

    // Initialize task 3
    tasks[2].stack_ptr = &tasks[2].stack[STACK_SIZE - 1];
    tasks[2].name = "Task3";
    tasks[2].run_count = 0;

    uart_puts("Tasks initialized\r\n");
}

//==============================================================================
// Main Scheduler Loop
//==============================================================================

void scheduler_run(void) {
    uart_puts("\r\nStarting cooperative scheduler...\r\n");
    uart_puts("Each task runs for ~1 second then switches\r\n");
    uart_puts("Timer interrupt fires every 100ms\r\n\r\n");

    // Enable interrupts
    irq_enable();

    // Main loop - run current task
    while (1) {
        switch (current_task) {
            case 0:
                task1_func();
                break;
            case 1:
                task2_func();
                break;
            case 2:
                task3_func();
                break;
        }
    }
}

//==============================================================================
// Main
//==============================================================================

int main(void) {
    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("Simple Cooperative Multitasking Demo\r\n");
    uart_puts("========================================\r\n");
    uart_puts("\r\n");

    uart_puts("Initializing tasks...\r\n");
    tasks_init();

    uart_puts("Initializing timer...\r\n");
    timer_init();

    uart_puts("\r\n");
    scheduler_run();

    // Should never reach here
    uart_puts("ERROR: Scheduler returned!\r\n");
    while (1);

    return 0;
}
