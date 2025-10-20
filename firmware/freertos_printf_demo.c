/*
 * FreeRTOS Printf Demo for PicoRV32
 *
 * Demonstrates FreeRTOS multitasking using newlib printf() for output.
 * This validates that newlib is statically linked and printf works correctly
 * in a multi-threaded environment.
 *
 * Features:
 * - 3 tasks printing formatted messages with printf()
 * - Demonstrates floating point formatting
 * - Shows task priorities and scheduling
 * - Uses printf() instead of custom uart functions
 * - Uses FreeRTOS macros (portNOP) instead of raw assembly
 */

#include <stdint.h>
#include <stdio.h>
#include <FreeRTOS.h>
#include <task.h>

//==============================================================================
// Hardware Registers
//==============================================================================

#define LED_CONTROL     (*(volatile uint32_t*)0x80000010)

//==============================================================================
// Task 1: Counter Task (Low Priority)
//==============================================================================

void vTask1_Counter(void *pvParameters) {
    (void)pvParameters;
    uint32_t count = 0;

    printf("Task1 (Counter): Started with priority %d\r\n", 
           (int)uxTaskPriorityGet(NULL));

    for (;;) {
        count++;
        printf("[Task1] Count = %lu (0x%08lX)\r\n", count, count);
        
        // Toggle LED0
        LED_CONTROL ^= 0x01;
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

//==============================================================================
// Task 2: Float Demo Task (Medium Priority)
//==============================================================================

void vTask2_FloatDemo(void *pvParameters) {
    (void)pvParameters;
    float value = 3.14159f;
    uint32_t iteration = 0;

    printf("Task2 (Float): Started with priority %d\r\n", 
           (int)uxTaskPriorityGet(NULL));

    for (;;) {
        iteration++;
        value *= 1.1f;
        
        printf("[Task2] Iteration %lu: Float = %.4f\r\n", 
               iteration, value);
        
        // Toggle LED1
        LED_CONTROL ^= 0x02;
        
        // Reset value to prevent overflow
        if (value > 1000.0f) {
            value = 3.14159f;
            printf("[Task2] Value reset to %.5f\r\n", value);
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

//==============================================================================
// Task 3: System Status Task (High Priority)
//==============================================================================

void vTask3_SystemStatus(void *pvParameters) {
    (void)pvParameters;

    printf("Task3 (Status): Started with priority %d\r\n", 
           (int)uxTaskPriorityGet(NULL));

    for (;;) {
        printf("\r\n");
        printf("=== System Status ===\r\n");
        printf("Tick count:    %lu\r\n", (unsigned long)xTaskGetTickCount());
        printf("Task count:    %u\r\n", (unsigned int)uxTaskGetNumberOfTasks());
        printf("Free heap:     %u bytes\r\n", (unsigned int)xPortGetFreeHeapSize());
        printf("Min free heap: %u bytes\r\n", (unsigned int)xPortGetMinimumEverFreeHeapSize());
        printf("=====================\r\n");
        printf("\r\n");
        
        // Toggle LED2
        LED_CONTROL ^= 0x04;
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

//==============================================================================
// Main Application
//==============================================================================

// External function to initialize UART mutex for thread-safe printf
extern void syscalls_init_uart_mutex(void);

int main(void) {
    BaseType_t xReturned;

    // Initialize UART mutex for thread-safe printf in FreeRTOS
    syscalls_init_uart_mutex();

    // Print startup banner using printf()
    printf("\r\n");
    printf("========================================\r\n");
    printf("FreeRTOS Printf Demo for PicoRV32\r\n");
    printf("========================================\r\n");
    printf("\r\n");

    // Print configuration using printf()
    printf("FreeRTOS Configuration:\r\n");
    printf("  CPU Clock:    %lu Hz (%lu MHz)\r\n", 
           (unsigned long)configCPU_CLOCK_HZ, 
           (unsigned long)(configCPU_CLOCK_HZ / 1000000));
    printf("  Tick Rate:    %lu Hz (%lu ms period)\r\n", 
           (unsigned long)configTICK_RATE_HZ,
           (unsigned long)(1000 / configTICK_RATE_HZ));
    printf("  Max Priority: %d\r\n", (int)configMAX_PRIORITIES);
    printf("  Heap Size:    %u bytes (%u KB)\r\n", 
           (unsigned int)configTOTAL_HEAP_SIZE,
           (unsigned int)(configTOTAL_HEAP_SIZE / 1024));
    printf("\r\n");

    // Print newlib information
    printf("Newlib Integration:\r\n");
    printf("  printf() is statically linked from newlib\r\n");
    printf("  Supports %%d, %%u, %%lu, %%x, %%f formatting\r\n");
    printf("  Float test: pi = %.5f\r\n", 3.14159f);
    printf("\r\n");

    printf("Creating tasks...\r\n");

    // Create Task 1: Counter (priority 1)
    xReturned = xTaskCreate(
        vTask1_Counter,
        "Counter",
        configMINIMAL_STACK_SIZE * 3,  // Needs 3x for printf - 2x causes stack overflow
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task1: Counter created\r\n");
    } else {
        printf("  [FAIL] Task1: Counter creation failed\r\n");
    }

    // Create Task 2: Float Demo (priority 2)
    xReturned = xTaskCreate(
        vTask2_FloatDemo,
        "FloatDemo",
        configMINIMAL_STACK_SIZE * 3,  // Needs 3x for printf - 2x causes stack overflow
        NULL,
        2,
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task2: FloatDemo created\r\n");
    } else {
        printf("  [FAIL] Task2: FloatDemo creation failed\r\n");
    }

    // Create Task 3: System Status (priority 3 - highest)
    xReturned = xTaskCreate(
        vTask3_SystemStatus,
        "SystemStatus",
        configMINIMAL_STACK_SIZE * 3,  // Larger stack for printf
        NULL,
        3,
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task3: SystemStatus created\r\n");
    } else {
        printf("  [FAIL] Task3: SystemStatus creation failed\r\n");
    }

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

    // Use FreeRTOS portNOP() macro instead of raw assembly
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
