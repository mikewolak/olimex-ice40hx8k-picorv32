//==============================================================================
// Overlay Project: hello_world
// main.c - Main entry point for hello_world overlay
//
// Copyright (c) October 2025
//==============================================================================

#include "hardware.h"
#include "io.h"

// If using newlib/printf, uncomment:
// #include <stdio.h>
// #include <stdlib.h>

//==============================================================================
// Main Entry Point
//==============================================================================

int main(void) {
    // Test multiple UART output calls
    uart_puts("Hello from overlay!\r\n");
    uart_puts("Testing multiple calls...\r\n");
    uart_puts("Line 1\r\n");
    uart_puts("Line 2\r\n");
    uart_puts("Line 3\r\n");
    uart_puts("Overlay complete!\r\n");
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
 *    - Heap is 24KB at 0x7A000
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
 *    - Code/Data: 0x60000 - 0x77FFF (96KB max)
 *    - Stack:     0x78000 - 0x79FFF (8KB, grows down from 0x7A000)
 *    - Heap:      0x7A000 - 0x7FFFF (24KB, grows up)
 *    - See memory_config.h for details
 *
 * 7. Debugging:
 *    - Use 'make disasm' to view assembly
 *    - Use 'make size' to check memory usage
 *    - Check overlay.map for symbol locations
 */
