/*
 * FreeRTOS Curses Demo for PicoRV32
 *
 * Demonstrates FreeRTOS multitasking with curses-based display.
 * Each task updates its own quadrant on the screen in-place.
 *
 * Screen Layout (24x80):
 *   - Top-left (Task 1):     Counter demo
 *   - Top-right (Task 2):    Float demo
 *   - Bottom (Task 3):       System status
 *
 * Copyright (c) 2025 Michael Wolak
 * Email: mikewolak@gmail.com, mike@epromfoundry.com
 */

#include <FreeRTOS.h>
#include <task.h>
#include <stdint.h>
#include <stdio.h>
#include "../lib/incurses/curses.h"

//==============================================================================
// Hardware Definitions
//==============================================================================

#define LED_CONTROL    (*(volatile uint32_t*)0x80000010)
#define UART_TX_DATA   (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t*)0x80000004)
#define UART_RX_DATA   (*(volatile uint32_t*)0x80000008)
#define UART_RX_STATUS (*(volatile uint32_t*)0x8000000C)

//==============================================================================
// UART Functions (required by incurses library)
//==============================================================================

void uart_putc(char c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

int uart_getc_available(void) {
    return UART_RX_STATUS & 1;
}

char uart_getc(void) {
    while (!uart_getc_available());
    return UART_RX_DATA & 0xFF;
}

//==============================================================================
// Forward Declarations
//==============================================================================

void vTask1_Counter(void *pvParameters);
void vTask2_FloatDemo(void *pvParameters);
void vTask3_SystemStatus(void *pvParameters);
void vTask4_DisplayUpdate(void *pvParameters);
void update_display(void);

//==============================================================================
// Shared Variables (Protected by Critical Sections)
//==============================================================================

static volatile uint32_t task1_count = 0;
static volatile uint32_t task2_iteration = 0;
static volatile float task2_value = 0.0f;

//==============================================================================
// Task 1: Counter Display (Top-Left Quadrant)
//==============================================================================

void vTask1_Counter(void *pvParameters)
{
    (void)pvParameters;
    uint32_t count = 0;

    for (;;) {
        // Update shared variable
        taskENTER_CRITICAL();
        task1_count = count;
        taskEXIT_CRITICAL();

        // Toggle LED0
        LED_CONTROL ^= 0x01;

        count++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Task 2: Float Display (Top-Right Quadrant)
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

        // Update shared variables
        taskENTER_CRITICAL();
        task2_iteration = iteration;
        task2_value = value;
        taskEXIT_CRITICAL();

        // Toggle LED1
        LED_CONTROL ^= 0x02;

        iteration++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Task 3: System Status Display (Bottom Quadrant)
//==============================================================================

void vTask3_SystemStatus(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        // Just toggle LED - actual display happens in main loop
        LED_CONTROL ^= 0x04;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

//==============================================================================
// Display Update Function
//==============================================================================

void update_display(void)
{
    uint32_t count, iteration;
    float value;

    // Read shared variables atomically
    taskENTER_CRITICAL();
    count = task1_count;
    iteration = task2_iteration;
    value = task2_value;
    taskEXIT_CRITICAL();

    // Draw borders
    // Top border
    move(0, 0);
    for (int i = 0; i < 80; i++) addch('=');

    // Middle horizontal border
    move(12, 0);
    for (int i = 0; i < 80; i++) addch('-');

    // Vertical divider
    for (int i = 1; i < 12; i++) {
        move(i, 40);
        addch('|');
    }

    // Top-left quadrant: Task 1
    attron(A_REVERSE);
    move(1, 1);
    addstr(" TASK 1: COUNTER                       ");
    standend();

    move(3, 2);
    printw("Count (dec): %lu", (unsigned long)count);
    clrtoeol();

    move(4, 2);
    printw("Count (hex): 0x%08lX", (unsigned long)count);
    clrtoeol();

    move(6, 2);
    printw("LED0: %s", (LED_CONTROL & 0x01) ? "ON " : "OFF");
    clrtoeol();

    move(8, 2);
    printw("Update rate: 500ms");
    clrtoeol();

    move(10, 2);
    printw("Priority: 1");
    clrtoeol();

    // Top-right quadrant: Task 2
    attron(A_REVERSE);
    move(1, 41);
    addstr(" TASK 2: FLOAT DEMO                   ");
    standend();

    move(3, 42);
    printw("Iteration: %lu", (unsigned long)iteration);
    clrtoeol();

    move(4, 42);
    printw("Value: %.4f", value);
    clrtoeol();

    move(6, 42);
    printw("Increment: 0.1234");
    clrtoeol();

    move(8, 42);
    printw("LED1: %s", (LED_CONTROL & 0x02) ? "ON " : "OFF");
    clrtoeol();

    move(10, 42);
    printw("Priority: 2");
    clrtoeol();

    // Bottom quadrant: Task 3 (System Status)
    attron(A_REVERSE);
    move(13, 1);
    addstr(" TASK 3: SYSTEM STATUS                                                        ");
    standend();

    move(15, 2);
    printw("FreeRTOS Tick Count: %lu", (unsigned long)xTaskGetTickCount());
    clrtoeol();

    move(16, 2);
    printw("Number of Tasks:     %u", (unsigned int)uxTaskGetNumberOfTasks());
    clrtoeol();

    move(17, 2);
    printw("Free Heap:           %u bytes", (unsigned int)xPortGetFreeHeapSize());
    clrtoeol();

    move(18, 2);
    printw("Min Free Heap:       %u bytes", (unsigned int)xPortGetMinimumEverFreeHeapSize());
    clrtoeol();

    move(20, 2);
    printw("LED2: %s", (LED_CONTROL & 0x04) ? "ON " : "OFF");
    clrtoeol();

    move(21, 2);
    printw("Update rate: 500ms");
    clrtoeol();

    // Status line
    move(23, 0);
    attron(A_REVERSE);
    addstr(" FreeRTOS Curses Demo - All tasks running at 500ms intervals                   ");
    standend();

    refresh();
}

//==============================================================================
// Main Application
//==============================================================================

int main(void) {
    BaseType_t xReturned;

    // Initialize curses BEFORE creating tasks
    initscr();
    noecho();
    cbreak();
    curs_set(FALSE);  // Hide cursor

    // Clear screen
    clear();
    refresh();

    // Create Task 1: Counter
    xReturned = xTaskCreate(
        vTask1_Counter,
        "Counter",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        2,  // Priority 2
        NULL
    );

    if (xReturned != pdPASS) {
        move(12, 20);
        addstr("ERROR: Failed to create Task 1");
        refresh();
        for (;;) portNOP();
    }

    // Create Task 2: Float Demo
    xReturned = xTaskCreate(
        vTask2_FloatDemo,
        "FloatDemo",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        2,  // Priority 2
        NULL
    );

    if (xReturned != pdPASS) {
        move(13, 20);
        addstr("ERROR: Failed to create Task 2");
        refresh();
        for (;;) portNOP();
    }

    // Create Task 3: System Status
    xReturned = xTaskCreate(
        vTask3_SystemStatus,
        "SystemStatus",
        configMINIMAL_STACK_SIZE * 2,
        NULL,
        2,  // Priority 2
        NULL
    );

    if (xReturned != pdPASS) {
        move(14, 20);
        addstr("ERROR: Failed to create Task 3");
        refresh();
        for (;;) portNOP();
    }

    // Create Task 4: Display Update (same priority for fair scheduling)
    xReturned = xTaskCreate(
        vTask4_DisplayUpdate,
        "Display",
        configMINIMAL_STACK_SIZE * 3,
        NULL,
        2,  // Priority 2 (same as others for time-slicing)
        NULL
    );

    if (xReturned != pdPASS) {
        // Can't use curses yet - just hang
        for (;;) portNOP();
    }

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    for (;;) {
        portNOP();
    }

    return 0;
}

//==============================================================================
// Task 4: Display Update Task (runs continuously)
//==============================================================================

void vTask4_DisplayUpdate(void *pvParameters)
{
    (void)pvParameters;

    for (;;) {
        // Update display every 100ms for smooth refresh
        update_display();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//==============================================================================
// FreeRTOS Idle Hook (called when no tasks are ready)
//==============================================================================

void vApplicationIdleHook(void)
{
    // Do nothing - display updates in dedicated task
    portNOP();
}
