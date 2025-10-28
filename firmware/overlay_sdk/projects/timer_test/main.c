//==============================================================================
// Overlay Project: timer_test
// Timer Interrupt Clock Demo - Overlay Version
//
// Tests timer interrupt handling in overlay environment.
// This is a port of firmware/timer_clock.c adapted for overlays.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "hardware.h"
#include "io.h"
#include <stdio.h>

//==============================================================================
// Timer Register Definitions - EXACT COPY from timer_clock.c
//==============================================================================
#define TIMER_BASE          0x80000020
#define TIMER_CR            (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR            (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_PSC           (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_ARR           (*(volatile uint32_t*)(TIMER_BASE + 0x0C))
#define TIMER_CNT           (*(volatile uint32_t*)(TIMER_BASE + 0x10))

// Timer control bits
#define TIMER_CR_ENABLE     (1 << 0)
#define TIMER_CR_ONE_SHOT   (1 << 1)
#define TIMER_SR_UIF        (1 << 0)

//==============================================================================
// Timer Helper Functions - EXACT COPY from timer_clock.c
//==============================================================================

static void timer_clock_init(void) {
    TIMER_CR = 0;               // Disable timer
    TIMER_SR = TIMER_SR_UIF;    // Clear any pending interrupt
}

static void timer_clock_config(uint16_t psc, uint32_t arr) {
    TIMER_PSC = psc;
    TIMER_ARR = arr;
}

static void timer_clock_start(void) {
    TIMER_CR = TIMER_CR_ENABLE;  // Enable, continuous mode
}

static void timer_clock_clear_irq(void) {
    TIMER_SR = TIMER_SR_UIF;     // Write 1 to clear
}

//==============================================================================
// Clock State (updated by interrupt)
//==============================================================================

volatile uint32_t frames = 0;   // Frame counter (0-59, increments at 60 Hz)
volatile uint32_t seconds = 0;  // Seconds counter (0-59)
volatile uint32_t minutes = 0;  // Minutes counter (0-59)
volatile uint32_t hours = 0;    // Hours counter (0-23)

//==============================================================================
// Timer Interrupt Handler
//
// This is called via overlay_timer_irq_handler pointer at 0x28000
// which is registered by main() before starting the timer.
//==============================================================================

void timer_irq_handler(void) {
    // CRITICAL: Clear the interrupt source FIRST
    timer_clock_clear_irq();

    // Update frame counter (0-59)
    frames++;
    if (frames >= 60) {
        frames = 0;

        // Update seconds
        seconds++;
        if (seconds >= 60) {
            seconds = 0;

            // Update minutes
            minutes++;
            if (minutes >= 60) {
                minutes = 0;

                // Update hours
                hours++;
                if (hours >= 24) {
                    hours = 0;
                }
            }
        }
    }
}

//==============================================================================
// Print clock value to UART
//==============================================================================

void print_clock(void) {
    // Format: HH:MM:SS:FF  (FF = frame, 0-59)

    // Hours (00-23)
    putchar('0' + (hours / 10));
    putchar('0' + (hours % 10));
    putchar(':');

    // Minutes (00-59)
    putchar('0' + (minutes / 10));
    putchar('0' + (minutes % 10));
    putchar(':');

    // Seconds (00-59)
    putchar('0' + (seconds / 10));
    putchar('0' + (seconds % 10));
    putchar(':');

    // Frames (00-59, 60 Hz = 1/60 second resolution)
    putchar('0' + (frames / 10));
    putchar('0' + (frames % 10));

    putchar('\r');  // Carriage return (no newline, overwrite same line)
}

//==============================================================================
// PicoRV32 IRQ Enable (overlay version)
//==============================================================================

static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

//==============================================================================
// Main Entry Point
//==============================================================================

int main(void) {
    // Print banner
    printf("\r\n");
    printf("==========================================\r\n");
    printf("Timer Interrupt Clock Demo (OVERLAY)\r\n");
    printf("PicoRV32 @ 50 MHz with Timer Peripheral\r\n");
    printf("==========================================\r\n");
    printf("\r\n");

    // CRITICAL: Register our timer interrupt handler with the firmware
    // The firmware's IRQ handler will call this function pointer at 0x28000
    printf("Registering timer IRQ handler at 0x28000...\r\n");
    void (**overlay_timer_irq_handler_ptr)(void) = (void (**)(void))0x28000;
    *overlay_timer_irq_handler_ptr = timer_irq_handler;

    printf("Configuring timer for 60 Hz interrupts...\r\n");

    // Initialize timer peripheral
    timer_clock_init();

    // Configure timer for 60 Hz (16.67ms period) - EXACT from timer_clock.c
    // System clock: 50 MHz
    // Prescaler: 49 (divide by 50) → 1 MHz tick rate
    // Auto-reload: 16666 → 1,000,000 / 16,667 = 59.998 Hz ≈ 60 Hz
    timer_clock_config(49, 16666);

    printf("Timer configured: PSC=49, ARR=16666 (60 Hz)\r\n");
    printf("\r\n");

    // Enable Timer IRQ (IRQ[0])
    // PicoRV32 IRQ mask: 1 = masked (disabled), 0 = unmasked (enabled)
    // We want to ENABLE IRQ[0], so clear bit 0 in mask
    printf("Enabling Timer IRQ[0]...\r\n");
    irq_enable();  // Enable all interrupts (clear all mask bits)

    // Start timer (continuous mode)
    printf("Starting timer...\r\n");
    timer_clock_start();

    printf("\r\n");
    printf("Clock running! (HH:MM:SS:FF format, 60 FPS)\r\n");
    printf("Press any key to stop and return to menu.\r\n");
    printf("\r\n");

    // Save last frame count to detect changes
    uint32_t last_frames = frames;

    // Main loop: Print clock when frame counter changes
    // Run for limited time or until keypress
    uint32_t loop_count = 0;
    const uint32_t MAX_LOOPS = 600;  // ~10 seconds at 60 FPS

    while (loop_count < MAX_LOOPS) {
        // Check if interrupt updated the frame counter
        if (frames != last_frames) {
            last_frames = frames;
            print_clock();
            loop_count++;
        }

        // Check for keypress to exit early
        if (uart_getc_available()) {
            uart_getc();  // Consume the character
            break;
        }
    }

    // Stop timer
    printf("\r\n\r\n");
    printf("Stopping timer...\r\n");
    TIMER_CR = 0;  // Disable timer

    // Unregister our timer interrupt handler
    printf("Unregistering timer IRQ handler...\r\n");
    *overlay_timer_irq_handler_ptr = 0;

    printf("\r\n");
    printf("Timer test complete!\r\n");
    printf("Final time: %02u:%02u:%02u\r\n",
           (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds);
    printf("\r\n");
    printf("Press any key to return to menu...\r\n");

    // Wait for keypress before returning
    while (!uart_getc_available());
    uart_getc();

    printf("\r\nReturning to SD Card Manager...\r\n");

    // Return to SD Card Manager cleanly
    return 0;
}
