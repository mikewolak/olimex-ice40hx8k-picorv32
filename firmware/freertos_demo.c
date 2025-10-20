/*
 * FreeRTOS Multi-Task Demo for PicoRV32
 *
 * Demonstrates FreeRTOS multitasking with:
 * - 3 LED blink tasks at different rates
 * - UART status reporting task
 * - Proper task priorities
 *
 * NOTE: Context switching implemented, but scheduler startup needs completion.
 * This demo will compile and link, showing full FreeRTOS integration.
 */

#include <stdint.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>

//==============================================================================
// Hardware Registers
//==============================================================================

#define UART_TX_DATA    (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS  (*(volatile uint32_t*)0x80000004)
#define LED_CONTROL     (*(volatile uint32_t*)0x80000010)

//==============================================================================
// UART Functions
//==============================================================================

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

void uart_print_dec(uint32_t val) {
    char buf[12];
    int i = 0;

    if (val == 0) {
        uart_putc('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

//==============================================================================
// Task 1: Fast LED Blinker (500ms, LED0)
//==============================================================================

void vTask1_FastBlink(void *pvParameters) {
    (void)pvParameters;

    uart_puts("Task1: Fast blinker started (500ms, LED0)\r\n");

    for (;;) {
        LED_CONTROL = 0x01;  // LED0 on
        vTaskDelay(pdMS_TO_TICKS(500));

        LED_CONTROL = 0x00;  // LED0 off
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Task 2: Medium LED Blinker (1000ms, LED1)
//==============================================================================

void vTask2_MediumBlink(void *pvParameters) {
    (void)pvParameters;

    uart_puts("Task2: Medium blinker started (1000ms, LED1)\r\n");

    for (;;) {
        LED_CONTROL = 0x02;  // LED1 on
        vTaskDelay(pdMS_TO_TICKS(1000));

        LED_CONTROL = 0x00;  // LED1 off
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//==============================================================================
// Task 3: Slow LED Blinker (2000ms, LED2)
//==============================================================================

void vTask3_SlowBlink(void *pvParameters) {
    (void)pvParameters;

    uart_puts("Task3: Slow blinker started (2000ms, LED2)\r\n");

    for (;;) {
        LED_CONTROL = 0x04;  // LED2 on
        vTaskDelay(pdMS_TO_TICKS(2000));

        LED_CONTROL = 0x00;  // LED2 off
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

//==============================================================================
// Task 4: Status Reporter (5000ms)
//==============================================================================

void vTask4_StatusReport(void *pvParameters) {
    (void)pvParameters;
    uint32_t count = 0;

    uart_puts("Task4: Status reporter started (5000ms)\r\n");

    for (;;) {
        count++;

        uart_puts("\r\n--- System Status ---\r\n");
        uart_puts("Uptime cycles: ");
        uart_print_dec(count);
        uart_puts("\r\n");

        uart_puts("Tick count: ");
        uart_print_dec(xTaskGetTickCount());
        uart_puts("\r\n");

        uart_puts("Task count: ");
        uart_print_dec(uxTaskGetNumberOfTasks());
        uart_puts("\r\n");

        uart_puts("Free heap: ");
        uart_print_dec(xPortGetFreeHeapSize());
        uart_puts(" bytes\r\n");

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

//==============================================================================
// Main Application
//==============================================================================

int main(void) {
    BaseType_t xReturned;

    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("FreeRTOS Multi-Task Demo for PicoRV32\r\n");
    uart_puts("========================================\r\n");
    uart_puts("\r\n");

    // Print FreeRTOS configuration
    uart_puts("FreeRTOS Configuration:\r\n");
    uart_puts("  CPU Clock:    ");
    uart_print_hex(configCPU_CLOCK_HZ);
    uart_puts(" Hz (50 MHz)\r\n");

    uart_puts("  Tick Rate:    ");
    uart_print_hex(configTICK_RATE_HZ);
    uart_puts(" Hz (1 ms)\r\n");

    uart_puts("  Max Priority: ");
    uart_print_dec(configMAX_PRIORITIES);
    uart_puts("\r\n");

    uart_puts("  Heap Size:    ");
    uart_print_dec(configTOTAL_HEAP_SIZE);
    uart_puts(" bytes\r\n");

    uart_puts("\r\n");
    uart_puts("Creating tasks...\r\n");

    // Create Task 1: Fast blink (priority 1)
    xReturned = xTaskCreate(
        vTask1_FastBlink,
        "FastBlink",
        configMINIMAL_STACK_SIZE,
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        uart_puts("  [OK] Task1: FastBlink created\r\n");
    } else {
        uart_puts("  [FAIL] Task1: FastBlink creation failed\r\n");
    }

    // Create Task 2: Medium blink (priority 1)
    xReturned = xTaskCreate(
        vTask2_MediumBlink,
        "MediumBlink",
        configMINIMAL_STACK_SIZE,
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        uart_puts("  [OK] Task2: MediumBlink created\r\n");
    } else {
        uart_puts("  [FAIL] Task2: MediumBlink creation failed\r\n");
    }

    // Create Task 3: Slow blink (priority 1)
    xReturned = xTaskCreate(
        vTask3_SlowBlink,
        "SlowBlink",
        configMINIMAL_STACK_SIZE,
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        uart_puts("  [OK] Task3: SlowBlink created\r\n");
    } else {
        uart_puts("  [FAIL] Task3: SlowBlink creation failed\r\n");
    }

    // Create Task 4: Status reporter (priority 2 - higher)
    xReturned = xTaskCreate(
        vTask4_StatusReport,
        "StatusReport",
        configMINIMAL_STACK_SIZE * 2,  // Larger stack for printf
        NULL,
        2,
        NULL
    );

    if (xReturned == pdPASS) {
        uart_puts("  [OK] Task4: StatusReport created\r\n");
    } else {
        uart_puts("  [FAIL] Task4: StatusReport creation failed\r\n");
    }

    uart_puts("\r\n");
    uart_puts("Total tasks created: ");
    uart_print_dec(uxTaskGetNumberOfTasks());
    uart_puts("\r\n");

    uart_puts("Free heap after task creation: ");
    uart_print_dec(xPortGetFreeHeapSize());
    uart_puts(" bytes\r\n");

    uart_puts("\r\n");
    uart_puts("Starting FreeRTOS scheduler...\r\n");
    uart_puts("NOTE: Scheduler startup not fully implemented yet.\r\n");
    uart_puts("Tasks are created but won't run until Task 5 complete.\r\n");
    uart_puts("\r\n");

    // Start the FreeRTOS scheduler
    // This will initialize timer and enable interrupts
    // TODO: Need to implement first task jump (Task 5)
    vTaskStartScheduler();

    // Should never reach here if scheduler starts successfully
    uart_puts("ERROR: Scheduler returned to main!\r\n");

    for (;;) {
        __asm__ volatile ("nop");
    }

    return 0;
}

//==============================================================================
// FreeRTOS Idle Hook (called when no tasks are ready)
//==============================================================================

void vApplicationIdleHook(void)
{
    __asm__ volatile ("nop");
}
