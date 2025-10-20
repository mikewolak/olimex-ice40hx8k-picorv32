/*
 * FreeRTOS Queue Demo for PicoRV32
 *
 * Demonstrates proper task communication using FreeRTOS queues.
 * Multiple data-generating tasks send messages to a single printer task.
 *
 * Architecture:
 * - 3 data generator tasks (Counter, Float, Status) create unique data
 * - 1 printer task receives messages via queue and prints them
 * - Only the printer task calls printf() - no mutex contention
 * - Demonstrates proper producer-consumer pattern
 *
 * Benefits:
 * - Better separation of concerns (compute vs I/O)
 * - Only one task does I/O, others focus on data generation
 * - Queue naturally handles synchronization
 * - More scalable design pattern
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>

//==============================================================================
// Hardware Registers
//==============================================================================

#define LED_CONTROL     (*(volatile uint32_t*)0x80000010)

//==============================================================================
// Message Queue Structure
//==============================================================================

#define MSG_MAX_LEN 120

typedef enum {
    MSG_COUNTER,
    MSG_FLOAT,
    MSG_STATUS,
    MSG_STARTUP
} MessageType_t;

typedef struct {
    MessageType_t type;
    char text[MSG_MAX_LEN];
} Message_t;

// Queue handle (shared between tasks)
static QueueHandle_t xPrintQueue = NULL;

//==============================================================================
// Task 1: Counter Data Generator
//==============================================================================

void vTask1_CounterGenerator(void *pvParameters) {
    (void)pvParameters;
    uint32_t count = 0;
    Message_t msg;

    msg.type = MSG_STARTUP;
    snprintf(msg.text, MSG_MAX_LEN, "Task1 (Counter): Started with priority %d",
             (int)uxTaskPriorityGet(NULL));
    xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

    for (;;) {
        count++;

        // Generate message data
        msg.type = MSG_COUNTER;
        snprintf(msg.text, MSG_MAX_LEN, "[Task1] Count = %lu (0x%08lX)",
                 (unsigned long)count, (unsigned long)count);

        // Send to printer task
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Toggle LED0
        LED_CONTROL ^= 0x01;

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

//==============================================================================
// Task 2: Float Data Generator
//==============================================================================

void vTask2_FloatGenerator(void *pvParameters) {
    (void)pvParameters;
    float value = 3.14159f;
    uint32_t iteration = 0;
    Message_t msg;

    msg.type = MSG_STARTUP;
    snprintf(msg.text, MSG_MAX_LEN, "Task2 (Float): Started with priority %d",
             (int)uxTaskPriorityGet(NULL));
    xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

    for (;;) {
        iteration++;
        value *= 1.1f;

        // Generate message data
        msg.type = MSG_FLOAT;
        snprintf(msg.text, MSG_MAX_LEN, "[Task2] Iteration %lu: Float = %.4f",
                 (unsigned long)iteration, value);

        // Send to printer task
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Toggle LED1
        LED_CONTROL ^= 0x02;

        // Reset value to prevent overflow
        if (value > 1000.0f) {
            value = 3.14159f;
            msg.type = MSG_FLOAT;
            snprintf(msg.text, MSG_MAX_LEN, "[Task2] Value reset to %.5f", value);
            xQueueSend(xPrintQueue, &msg, portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

//==============================================================================
// Task 3: System Status Data Generator
//==============================================================================

void vTask3_StatusGenerator(void *pvParameters) {
    (void)pvParameters;
    Message_t msg;
    char temp[MSG_MAX_LEN];

    msg.type = MSG_STARTUP;
    snprintf(msg.text, MSG_MAX_LEN, "Task3 (Status): Started with priority %d",
             (int)uxTaskPriorityGet(NULL));
    xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

    for (;;) {
        // Send status header
        msg.type = MSG_STATUS;
        snprintf(msg.text, MSG_MAX_LEN, "\r\n=== System Status ===");
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Send tick count
        snprintf(msg.text, MSG_MAX_LEN, "Tick count:    %lu",
                 (unsigned long)xTaskGetTickCount());
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Send task count
        snprintf(msg.text, MSG_MAX_LEN, "Task count:    %u",
                 (unsigned int)uxTaskGetNumberOfTasks());
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Send free heap
        snprintf(msg.text, MSG_MAX_LEN, "Free heap:     %u bytes",
                 (unsigned int)xPortGetFreeHeapSize());
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Send min free heap
        snprintf(msg.text, MSG_MAX_LEN, "Min free heap: %u bytes",
                 (unsigned int)xPortGetMinimumEverFreeHeapSize());
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Send queue usage
        UBaseType_t waiting = uxQueueMessagesWaiting(xPrintQueue);
        snprintf(msg.text, MSG_MAX_LEN, "Queue waiting: %u messages",
                 (unsigned int)waiting);
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Send footer
        snprintf(msg.text, MSG_MAX_LEN, "=====================\r\n");
        xQueueSend(xPrintQueue, &msg, portMAX_DELAY);

        // Toggle LED2
        LED_CONTROL ^= 0x04;

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

//==============================================================================
// Task 4: Printer (ONLY task that calls printf)
//==============================================================================

void vTask4_Printer(void *pvParameters) {
    (void)pvParameters;
    Message_t msg;

    for (;;) {
        // Wait for message from queue
        if (xQueueReceive(xPrintQueue, &msg, portMAX_DELAY) == pdTRUE) {
            // Print the message
            printf("%s\r\n", msg.text);
        }
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
    printf("FreeRTOS Queue Demo for PicoRV32\r\n");
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

    printf("Demo Architecture:\r\n");
    printf("  - 3 data generator tasks (Counter, Float, Status)\r\n");
    printf("  - 1 printer task (ONLY task that calls printf)\r\n");
    printf("  - Messages sent via FreeRTOS queue\r\n");
    printf("  - Queue capacity: 20 messages\r\n");
    printf("\r\n");

    // Create message queue (capacity: 20 messages)
    xPrintQueue = xQueueCreate(20, sizeof(Message_t));

    if (xPrintQueue == NULL) {
        printf("ERROR: Failed to create print queue!\r\n");
        for (;;) portNOP();
    }
    printf("  [OK] Print queue created (20 messages)\r\n");

    printf("\r\n");
    printf("Creating tasks...\r\n");

    // Create Task 1: Counter Generator (priority 1)
    xReturned = xTaskCreate(
        vTask1_CounterGenerator,
        "CountGen",
        configMINIMAL_STACK_SIZE * 3,  // Needs stack for snprintf
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task1: Counter Generator created\r\n");
    } else {
        printf("  [FAIL] Task1: Counter Generator creation failed\r\n");
    }

    // Create Task 2: Float Generator (priority 1)
    xReturned = xTaskCreate(
        vTask2_FloatGenerator,
        "FloatGen",
        configMINIMAL_STACK_SIZE * 3,  // Needs stack for snprintf
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task2: Float Generator created\r\n");
    } else {
        printf("  [FAIL] Task2: Float Generator creation failed\r\n");
    }

    // Create Task 3: Status Generator (priority 1)
    xReturned = xTaskCreate(
        vTask3_StatusGenerator,
        "StatusGen",
        configMINIMAL_STACK_SIZE * 3,
        NULL,
        1,
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task3: Status Generator created\r\n");
    } else {
        printf("  [FAIL] Task3: Status Generator creation failed\r\n");
    }

    // Create Task 4: Printer (priority 2 - higher to drain queue quickly)
    xReturned = xTaskCreate(
        vTask4_Printer,
        "Printer",
        configMINIMAL_STACK_SIZE * 3,  // Needs stack for printf
        NULL,
        2,  // Higher priority to ensure messages are printed promptly
        NULL
    );

    if (xReturned == pdPASS) {
        printf("  [OK] Task4: Printer created (priority 2)\r\n");
    } else {
        printf("  [FAIL] Task4: Printer creation failed\r\n");
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
