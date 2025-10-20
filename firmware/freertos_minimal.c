/*
 * Minimal FreeRTOS test for PicoRV32
 *
 * This is a minimal proof-of-concept to verify FreeRTOS compiles and links.
 * Creates a simple task to ensure FreeRTOS kernel code is actually linked.
 * NOTE: Scheduler won't actually run yet - context switching not implemented.
 */

#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>

/* Hardware */
#define UART_TX_DATA    (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS  (*(volatile uint32_t*)0x80000004)

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

/* External diagnostic counter from freertos_irq.c */
extern volatile uint32_t timer_irq_count;

/* Simple test task */
void vTestTask(void *pvParameters) {
    (void)pvParameters;
    uint32_t counter = 0;
    TickType_t tickBefore, tickAfter;

    uart_puts("Task started\r\n\r\n");

    for (;;) {
        tickBefore = xTaskGetTickCount();

        // Print every time to see exactly what's happening
        uart_puts("Loop #");
        uart_print_hex(counter);
        uart_puts(": Tick=");
        uart_print_hex(tickBefore);
        uart_puts(", IRQ=");
        uart_print_hex(timer_irq_count);
        uart_puts(", Calling vTaskDelay(");
        uart_print_hex(1000);
        uart_puts(")...\r\n");

        counter++;

        vTaskDelay(1000);  // Should delay 1000 ticks = 1 second

        tickAfter = xTaskGetTickCount();

        uart_puts("  -> Woke up after ");
        uart_print_hex(tickAfter - tickBefore);
        uart_puts(" ticks (expected ");
        uart_print_hex(1000);
        uart_puts(")\r\n\r\n");
    }
}

int main(void) {
    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("FreeRTOS Minimal Test for PicoRV32\r\n");
    uart_puts("========================================\r\n");
    uart_puts("\r\n");

    uart_puts("FreeRTOS kernel compiled and linked!\r\n");
    uart_puts("\r\n");

    /* Print configuration */
    uart_puts("Configuration:\r\n");
    uart_puts("  CPU Clock:    ");
    uart_print_hex(configCPU_CLOCK_HZ);
    uart_puts(" Hz (50 MHz)\r\n");

    uart_puts("  Tick Rate:    ");
    uart_print_hex(configTICK_RATE_HZ);
    uart_puts(" Hz (");
    uart_print_hex(configTICK_RATE_HZ);
    uart_puts(" = 1000 for 1ms tick)\r\n");

    uart_puts("  pdMS_TO_TICKS(1000) = ");
    uart_print_hex(pdMS_TO_TICKS(1000));
    uart_puts(" ticks\r\n");

    uart_puts("  Max Priority: ");
    uart_print_hex(configMAX_PRIORITIES);
    uart_puts("\r\n");

    uart_puts("  Heap Size:    ");
    uart_print_hex(configTOTAL_HEAP_SIZE);
    uart_puts(" bytes\r\n");

    uart_puts("\r\n");

    /* Try to create a task (this will link in FreeRTOS kernel code) */
    uart_puts("Creating test task...\r\n");

    TaskHandle_t xHandle = NULL;
    BaseType_t xReturned = xTaskCreate(
        vTestTask,           /* Task function */
        "TestTask",          /* Task name */
        128,                 /* Stack size (words) */
        NULL,                /* Parameters */
        1,                   /* Priority */
        &xHandle             /* Task handle */
    );

    if (xReturned == pdPASS) {
        uart_puts("Task created successfully!\r\n");
        uart_puts("Task handle: ");
        uart_print_hex((uint32_t)xHandle);
        uart_puts("\r\n");
    } else {
        uart_puts("ERROR: Task creation failed!\r\n");
    }

    uart_puts("\r\n");
    uart_puts("Starting FreeRTOS scheduler...\r\n");
    uart_puts("\r\n");

    /* Start the FreeRTOS scheduler - this should never return */
    vTaskStartScheduler();

    /* Should never reach here */
    uart_puts("ERROR: Scheduler returned to main!\r\n");
    for (;;) {
        __asm__ volatile ("nop");
    }

    return 0;
}

/* FreeRTOS Idle Hook (called when no tasks are ready) */
void vApplicationIdleHook(void)
{
    __asm__ volatile ("nop");
}
