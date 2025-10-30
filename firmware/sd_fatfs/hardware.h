//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// Hardware Register Definitions
//
// This header defines all memory-mapped I/O registers for the PicoRV32
// FPGA platform peripherals:
//   - UART: Serial communication
//   - Timer: 32-bit countdown timer with interrupt
//   - LEDs: 2 user LEDs
//   - Buttons: 2 user buttons
//   - SPI Master: For SD card interface
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>

//==============================================================================
// System Configuration
//==============================================================================

#define SYSTEM_CLOCK_HZ     50000000    // 50 MHz system clock

//==============================================================================
// UART Peripheral (0x80000000)
//
// Simple UART with TX/RX buffers and status flags
// Default baud rate: 115200 bps
//==============================================================================

#define UART_BASE       0x80000000

#define UART_TX_DATA    (*(volatile uint32_t*)(UART_BASE + 0x00))
#define UART_TX_STATUS  (*(volatile uint32_t*)(UART_BASE + 0x04))
#define UART_RX_DATA    (*(volatile uint32_t*)(UART_BASE + 0x08))
#define UART_RX_STATUS  (*(volatile uint32_t*)(UART_BASE + 0x0C))

// UART status bits
#define UART_TX_BUSY    (1 << 0)  // TX busy (wait before sending)
#define UART_RX_READY   (1 << 0)  // RX data available

//==============================================================================
// Timer Peripheral (0x80000020)
//
// 32-bit countdown timer with interrupt generation
// Timer decrements at system clock rate (50 MHz)
// Generates interrupt when counter reaches zero
//==============================================================================

#define TIMER_BASE      0x80000020

#define TIMER_CTRL      (*(volatile uint32_t*)(TIMER_BASE + 0x00))
#define TIMER_STATUS    (*(volatile uint32_t*)(TIMER_BASE + 0x04))
#define TIMER_COUNTER   (*(volatile uint32_t*)(TIMER_BASE + 0x08))
#define TIMER_COMPARE   (*(volatile uint32_t*)(TIMER_BASE + 0x0C))

// Timer control bits
#define TIMER_ENABLE        (1 << 0)  // Enable timer
#define TIMER_IRQ_ENABLE    (1 << 1)  // Enable interrupt
#define TIMER_ONE_SHOT      (1 << 2)  // One-shot mode (vs continuous)

// Timer status bits
#define TIMER_SR_UIF        (1 << 0)  // Update interrupt flag (write 1 to clear)

// Timer helper macros
#define TIMER_MS_TO_TICKS(ms)   ((SYSTEM_CLOCK_HZ / 1000) * (ms))
#define TIMER_US_TO_TICKS(us)   ((SYSTEM_CLOCK_HZ / 1000000) * (us))

//==============================================================================
// LED Peripheral (0x80000010)
//
// Two user LEDs on the board
// Write 1 to turn LED on, 0 to turn off
//==============================================================================

#define LED_BASE        0x80000010

#define LED_REG         (*(volatile uint32_t*)LED_BASE)

// LED bit positions
#define LED0            (1 << 0)  // LED 0
#define LED1            (1 << 1)  // LED 1

//==============================================================================
// Button Peripheral (0x80000018)
//
// Two user buttons on the board
// Read 1 when button pressed, 0 when released
// Note: Check if pull-up or pull-down - may need inversion
//==============================================================================

#define BUTTON_BASE     0x80000018

#define BUTTON_REG      (*(volatile uint32_t*)BUTTON_BASE)

// Button bit positions
#define BUTTON0         (1 << 0)  // Button 0
#define BUTTON1         (1 << 1)  // Button 1

//==============================================================================
// SPI Master Peripheral (0x80000050)
//
// Non-FIFO version - gate-efficient design
// Supports 8 power-of-2 clock dividers from 50 MHz to 390 kHz
// Manual chip select control
// CPOL=0, CPHA=0 (mode 0) - configurable if needed
//==============================================================================

#define SPI_BASE        0x80000050

#define SPI_CTRL        (*(volatile uint32_t*)(SPI_BASE + 0x00))
#define SPI_DATA        (*(volatile uint32_t*)(SPI_BASE + 0x04))
#define SPI_STATUS      (*(volatile uint32_t*)(SPI_BASE + 0x08))
#define SPI_CS          (*(volatile uint32_t*)(SPI_BASE + 0x0C))
#define SPI_BURST       (*(volatile uint32_t*)(SPI_BASE + 0x10))  // Burst byte count (0-8192)

// SPI status bits
#define SPI_STATUS_BUSY (1 << 0)  // Transfer in progress
#define SPI_STATUS_DONE (1 << 1)  // Transfer complete
#define SPI_STATUS_BURST_MODE (1 << 2)  // Burst mode active

// SPI clock divider values (bits [4:2] of CTRL register)
#define SPI_CLK_50MHZ   (0 << 2)  // /1  = 50.0 MHz
#define SPI_CLK_25MHZ   (1 << 2)  // /2  = 25.0 MHz
#define SPI_CLK_12MHZ   (2 << 2)  // /4  = 12.5 MHz
#define SPI_CLK_6MHZ    (3 << 2)  // /8  = 6.25 MHz
#define SPI_CLK_3MHZ    (4 << 2)  // /16 = 3.125 MHz
#define SPI_CLK_1MHZ    (5 << 2)  // /32 = 1.562 MHz
#define SPI_CLK_781KHZ  (6 << 2)  // /64 = 781 kHz
#define SPI_CLK_390KHZ  (7 << 2)  // /128 = 390 kHz

#endif // HARDWARE_H
