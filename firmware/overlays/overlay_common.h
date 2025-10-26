//==============================================================================
// Overlay Common Header
//
// Common definitions and utilities for all overlay programs.
// Provides basic I/O functions and hardware access.
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef OVERLAY_COMMON_H
#define OVERLAY_COMMON_H

#include <stdint.h>

//==============================================================================
// Hardware Definitions (from hardware.h)
//==============================================================================

// UART registers
#define UART_BASE       0x80000000
#define UART_TX_DATA    (*(volatile uint32_t*)(UART_BASE + 0x00))
#define UART_TX_STATUS  (*(volatile uint32_t*)(UART_BASE + 0x04))
#define UART_RX_DATA    (*(volatile uint32_t*)(UART_BASE + 0x08))
#define UART_RX_STATUS  (*(volatile uint32_t*)(UART_BASE + 0x0C))

// LED register
#define LED_BASE        0x80000010
#define LED_REG         (*(volatile uint32_t*)(LED_BASE + 0x00))

// Timer registers
#define TIMER_BASE      0x80000020
#define TIMER_CTRL      (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_SR        (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_CNT       (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_COMPARE   (*(volatile uint32_t*)(TIMER_BASE + 0x0C))

//==============================================================================
// Basic I/O Functions
//==============================================================================

// UART output
static inline void overlay_putc(char c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

static inline void overlay_puts(const char *s) {
    while (*s) {
        overlay_putc(*s++);
    }
}

// UART input
static inline char overlay_getc(void) {
    while (!(UART_RX_STATUS & 1));  // Wait for data
    return UART_RX_DATA & 0xFF;
}

// Simple string formatting (no printf in overlays)
static inline void overlay_print_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    overlay_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        overlay_putc(hex[(val >> i) & 0xF]);
    }
}

static inline void overlay_print_dec(uint32_t val) {
    char buf[16];
    int i = 0;

    if (val == 0) {
        overlay_putc('0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        overlay_putc(buf[--i]);
    }
}

// Simple delay
static inline void overlay_delay(uint32_t cycles) {
    for (volatile uint32_t i = 0; i < cycles; i++);
}

//==============================================================================
// Overlay Entry Point Macro
//==============================================================================

// Every overlay MUST define an entry point using this macro
// Example:
//   OVERLAY_ENTRY(my_overlay_main)
//   void my_overlay_main(void) {
//       // Overlay code here
//   }

#define OVERLAY_ENTRY(func) \
    void __attribute__((section(".text.overlay_entry"))) _overlay_start(void) { \
        func(); \
    }

//==============================================================================
// BSS Initialization
//==============================================================================

// Overlays should call this to clear .bss section
extern uint32_t __bss_start;
extern uint32_t __bss_end;

static inline void overlay_init_bss(void) {
    uint32_t *bss = &__bss_start;
    uint32_t *bss_end = &__bss_end;

    while (bss < bss_end) {
        *bss++ = 0;
    }
}

#endif // OVERLAY_COMMON_H
