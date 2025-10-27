//==============================================================================
// Overlay Project: printf_demo
// main.c - Main entry point for printf_demo overlay
//
// Copyright (c) October 2025
//==============================================================================

#include "hardware.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>

//==============================================================================
// Main Entry Point
//==============================================================================

int main(void) {
    // Print "Hello, World!" 100 times
    for (int i = 1; i <= 100; i++) {
        printf("Hello, World!\r\n");
    }

    printf("\r\nPress any key to exit...\r\n");

    // Wait for key press (getch is non-blocking, so loop until we get a valid key)
    int ch;
    while (1) {
        ch = uart_getc_available();
        if (ch) {
            // Key available, read it
            uart_getc();
            break;
        }
    }

    return 0;
}

//==============================================================================
// Notes
//==============================================================================

/*
 * Overlay Development Tips:
 *
 * 1. I/O Functions (always available):
 *    - putchar(c)  - Output single character to UART
 *    - getchar()   - Read single character from UART (blocking)
 *    - puts(str)   - Output string to UART
 *
 * 2. Using printf (requires PIC sysroot):
 *    - Uncomment #include <stdio.h>
 *    - Build PIC sysroot first (see SDK documentation)
 *    - Use printf(), sprintf(), etc. as normal
 *
 * 3. Using malloc (requires PIC sysroot):
 *    - Uncomment #include <stdlib.h>
 *    - Heap is 24KB at 0x3A000
 *    - Use malloc(), free(), etc. as normal
 *
 * 4. Hardware Access:
 *    - All MMIO registers defined in hardware.h
 *    - Direct access: UART_TX_DATA, LED_REG, etc.
 *    - See hardware.h for complete list
 *
 * 5. Returning to SD Card Manager:
 *    - Always return 0 from main() or call exit(0)
 *    - Overlay will cleanly return to menu
 *    - Do NOT use infinite loops unless intended
 *
 * 6. Memory Layout:
 *    - Code/Data: 0x18000 - 0x37FFF (128KB max)
 *    - Stack:     0x38000 - 0x39FFF (8KB)
 *    - Heap:      0x3A000 - 0x3FFFF (24KB)
 *    - See memory_config.h for details
 *
 * 7. Debugging:
 *    - Use 'make disasm' to view assembly
 *    - Use 'make size' to check memory usage
 *    - Check overlay.map for symbol locations
 */
