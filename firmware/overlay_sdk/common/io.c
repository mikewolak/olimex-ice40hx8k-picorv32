//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// I/O Functions for All Peripherals
//
// Provides high-level functions for:
//   - UART: Serial communication (for incurses)
//   - Timer: Delay and timing functions
//   - LEDs: Visual indicators
//   - Buttons: User input (for future soft eject feature)
//   - SPI: Low-level transfer functions (used by sd_spi.c)
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#include "io.h"
#include "hardware.h"

//==============================================================================
// UART Functions (required by incurses library)
//==============================================================================

void uart_putc(char c) {
    while (UART_TX_STATUS & UART_TX_BUSY);
    UART_TX_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

int uart_getc_available(void) {
    return UART_RX_STATUS & UART_RX_READY;
}

char uart_getc(void) {
    while (!uart_getc_available());
    return UART_RX_DATA & 0xFF;
}

//==============================================================================
// Timer Functions
//==============================================================================

void timer_init(void) {
    TIMER_CTRL = 0;  // Disable timer
    TIMER_STATUS = TIMER_SR_UIF;  // Clear any pending interrupt
}

void timer_delay_ms(uint32_t ms) {
    TIMER_CTRL = 0;  // Disable timer
    TIMER_COMPARE = TIMER_MS_TO_TICKS(ms);
    TIMER_CTRL = TIMER_ENABLE | TIMER_ONE_SHOT;  // Start one-shot timer

    // Wait for timer to expire
    while (TIMER_CTRL & TIMER_ENABLE);

    TIMER_STATUS = TIMER_SR_UIF;  // Clear flag
}

void timer_delay_us(uint32_t us) {
    TIMER_CTRL = 0;  // Disable timer
    TIMER_COMPARE = TIMER_US_TO_TICKS(us);
    TIMER_CTRL = TIMER_ENABLE | TIMER_ONE_SHOT;  // Start one-shot timer

    // Wait for timer to expire
    while (TIMER_CTRL & TIMER_ENABLE);

    TIMER_STATUS = TIMER_SR_UIF;  // Clear flag
}

uint32_t timer_get_ticks(void) {
    return TIMER_COUNTER;
}

//==============================================================================
// LED Functions
//==============================================================================

void led_on(uint8_t led) {
    LED_REG |= (1 << led);
}

void led_off(uint8_t led) {
    LED_REG &= ~(1 << led);
}

void led_toggle(uint8_t led) {
    LED_REG ^= (1 << led);
}

void led_set(uint8_t led, uint8_t state) {
    if (state) {
        led_on(led);
    } else {
        led_off(led);
    }
}

uint8_t led_get(uint8_t led) {
    return (LED_REG >> led) & 1;
}

void led_set_all(uint8_t pattern) {
    LED_REG = pattern;
}

//==============================================================================
// Button Functions
//==============================================================================

uint8_t button_read(uint8_t button) {
    return (BUTTON_REG >> button) & 1;
}

uint8_t button_read_all(void) {
    return BUTTON_REG & 0x03;  // Only 2 buttons
}

uint8_t button_wait_press(uint8_t button) {
    // Wait for button to be released (if already pressed)
    while (button_read(button));

    // Wait for button to be pressed
    while (!button_read(button));

    // Simple debounce delay
    timer_delay_ms(20);

    return 1;
}

uint8_t button_wait_release(uint8_t button) {
    // Wait for button to be released
    while (button_read(button));

    // Simple debounce delay
    timer_delay_ms(20);

    return 1;
}

//==============================================================================
// SPI Functions (low-level, used by sd_spi.c)
//==============================================================================

void spi_init(uint32_t speed) {
    SPI_CTRL = speed;
    SPI_CS = 1;  // CS high (inactive)
}

void spi_set_speed(uint32_t speed) {
    SPI_CTRL = speed;
}

uint8_t spi_transfer(uint8_t data) {
    SPI_DATA = data;
    while (SPI_STATUS & SPI_STATUS_BUSY);
    return SPI_DATA & 0xFF;
}

void spi_cs_assert(void) {
    SPI_CS = 0;
}

void spi_cs_deassert(void) {
    SPI_CS = 1;
}
