//==============================================================================
// Hello World Overlay - Simple Test Program
//
// Demonstrates overlay functionality by printing a message and blinking LEDs.
// This is a minimal test to verify the overlay system works.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "overlay_common.h"

//==============================================================================
// Main Overlay Function
//==============================================================================

void hello_main(void) {
    // Initialize BSS (zero uninitialized data)
    overlay_init_bss();

    // Print banner
    overlay_puts("\n");
    overlay_puts("========================================\n");
    overlay_puts("Hello World Overlay\n");
    overlay_puts("========================================\n");
    overlay_puts("\n");

    // Show load address
    overlay_puts("Running from address: ");
    overlay_print_hex(0x00018000);
    overlay_puts("\n\n");

    // Blink LEDs and print messages
    overlay_puts("Blinking LEDs...\n");
    overlay_puts("(Press any key to exit)\n\n");

    uint32_t count = 0;

    while (1) {
        // Check for keypress
        if (UART_RX_STATUS & 1) {
            // Key pressed - exit overlay
            (void)UART_RX_DATA;  // Read and discard
            break;
        }

        // Toggle LEDs
        LED_REG = count & 0x03;

        // Print counter every 8 iterations
        if ((count & 0x07) == 0) {
            overlay_puts("Count: ");
            overlay_print_dec(count);
            overlay_puts("\r");
        }

        // Delay
        overlay_delay(500000);

        count++;

        // Auto-exit after 50 iterations (~25 seconds)
        if (count >= 50) {
            break;
        }
    }

    // Turn off LEDs
    LED_REG = 0;

    // Print exit message
    overlay_puts("\n\n");
    overlay_puts("========================================\n");
    overlay_puts("Overlay Exiting\n");
    overlay_puts("========================================\n");
    overlay_puts("\n");

    // Return to SD card manager
}

// Define entry point
OVERLAY_ENTRY(hello_main)
