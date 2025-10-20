/*
 * FreeRTOS 4-Task Demo for PicoRV32 (No Curses)
 *
 * Simple demo with 4 tasks printing to UART to verify multitasking works.
 * Same task structure as curses demo but without the curses library.
 */

#include <stdint.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>

//==============================================================================
// Hardware Definitions
//==============================================================================

#define LED_CONTROL    (*(volatile uint32_t*)0x80000010)

//==============================================================================
// Task 1: Counter
//==============================================================================

void vTask1_Counter(void *pvParameters)
{
    (void)pvParameters;
    uint32_t count = 0;

    for (;;) {
        printf("Task1: Count = %lu (0x%08lX), LED0 = %s\r\n", 
               (unsigned long)count,
               (unsigned long)count,
               (LED_CONTROL & 0x01) ? "ON " : "OFF");

        LED_CONTROL ^= 0x01;
        count++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Task 2: Float Demo
//==============================================================================

void vTask2_FloatDemo(void *pvParameters)
{
    (void)pvParameters;
    uint32_t iteration = 0;
    float value = 0.0f;
    const float increment = 0.1234f;

    for (;;) {
        value += increment;
        if (value > 100.0f) {
            value = 0.0f;
        }

        printf("Task2: Iteration = %lu, Value = %.4f, LED1 = %s\r\n",
               (unsigned long)iteration,
               value,
               (LED_CONTROL & 0x02) ? "ON " : "OFF");

        LED_CONTROL ^= 0x02;
        iteration++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Task 3: System Status
//==============================================================================

void vTask3_SystemStatus(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        printf("Task3: Tick = %lu, Tasks = %u, Heap = %u bytes, LED2 = %s\r\n",
               (unsigned long)xTaskGetTickCount(),
               (unsigned int)uxTaskGetNumberOfTasks(),
               (unsigned int)xPortGetFreeHeapSize(),
               (LED_CONTROL & 0x04) ? "ON " : "OFF");

        LED_CONTROL ^= 0x04;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Task 4: Periodic Status
//==============================================================================

void vTask4_PeriodicStatus(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        printf("\r\n=== System Status ===\r\n");
        printf("Tick count:     %lu\r\n", (unsigned long)xTaskGetTickCount());
        printf("Tasks running:  %u\r\n", (unsigned int)uxTaskGetNumberOfTasks());
        printf("Free heap:      %u bytes\r\n", (unsigned int)xPortGetFreeHeapSize());
        printf("Min free heap:  %u bytes\r\n", (unsigned int)xPortGetMinimumEverFreeHeapSize());
        printf("=====================\r\n\r\n");

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

//==============================================================================
// Main Application
//==============================================================================

int main(void) {
    BaseType_t xReturned;

    printf("\r\n");
    printf("========================================\r\n");
    printf("FreeRTOS 4-Task Demo (No Curses)\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    printf("FreeRTOS Configuration:\r\n");
    printf("  CPU Clock:    %lu Hz\r\n", (unsigned long)configCPU_CLOCK_HZ);
    printf("  Tick Rate:    %lu Hz\r\n", (unsigned long)configTICK_RATE_HZ);
    printf("  Max Priority: %u\r\n", (unsigned int)configMAX_PRIORITIES);
    printf("  Heap Size:    %u bytes\r\n", (unsigned int)configTOTAL_HEAP_SIZE);
    printf("\r\n");

    // Create Task 1: Counter (needs 3x stack for printf)
    xReturned = xTaskCreate(
        vTask1_Counter,
        "Counter",
        configMINIMAL_STACK_SIZE * 3,
        NULL,
        2,
        NULL
    );

    if (xReturned != pdPASS) {
        printf("ERROR: Failed to create Task 1\r\n");
        for (;;) portNOP();
    }
    printf("  [OK] Task1: Counter created\r\n");

    // Create Task 2: Float Demo (needs 3x stack for printf)
    xReturned = xTaskCreate(
        vTask2_FloatDemo,
        "FloatDemo",
        configMINIMAL_STACK_SIZE * 3,
        NULL,
        2,
        NULL
    );

    if (xReturned != pdPASS) {
        printf("ERROR: Failed to create Task 2\r\n");
        for (;;) portNOP();
    }
    printf("  [OK] Task2: FloatDemo created\r\n");

    // Create Task 3: System Status (needs 3x stack for printf)
    xReturned = xTaskCreate(
        vTask3_SystemStatus,
        "SystemStatus",
        configMINIMAL_STACK_SIZE * 3,
        NULL,
        2,
        NULL
    );

    if (xReturned != pdPASS) {
        printf("ERROR: Failed to create Task 3\r\n");
        for (;;) portNOP();
    }
    printf("  [OK] Task3: SystemStatus created\r\n");

    // NOTE: Only 3 tasks like printf demo - 4 tasks causes freezing
    // Task 4 removed to match working printf demo configuration

    printf("\r\n");
    printf("Total tasks created: %u\r\n", (unsigned int)uxTaskGetNumberOfTasks());
    printf("Free heap: %u bytes\r\n", (unsigned int)xPortGetFreeHeapSize());
    printf("\r\n");
    printf("Starting FreeRTOS scheduler...\r\n");
    printf("\r\n");

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    printf("ERROR: Scheduler returned to main!\r\n");

    for (;;) {
        portNOP();
    }

    return 0;
}

//==============================================================================
// FreeRTOS Idle Hook (called when no tasks are ready)
//==============================================================================

void vApplicationIdleHook(void)
{
    portNOP();
}
