//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// I/O Function Prototypes
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//==============================================================================

#ifndef IO_H
#define IO_H

#include <stdint.h>

//==============================================================================
// UART Functions
//==============================================================================

void uart_putc(char c);
void uart_puts(const char *s);
int uart_getc_available(void);
char uart_getc(void);

//==============================================================================
// Timer Functions
//==============================================================================

void timer_init(void);
void timer_delay_ms(uint32_t ms);
void timer_delay_us(uint32_t us);
uint32_t timer_get_ticks(void);

//==============================================================================
// LED Functions
//==============================================================================

void led_on(uint8_t led);
void led_off(uint8_t led);
void led_toggle(uint8_t led);
void led_set(uint8_t led, uint8_t state);
uint8_t led_get(uint8_t led);
void led_set_all(uint8_t pattern);

//==============================================================================
// Button Functions
//==============================================================================

uint8_t button_read(uint8_t button);
uint8_t button_read_all(void);
uint8_t button_wait_press(uint8_t button);
uint8_t button_wait_release(uint8_t button);

//==============================================================================
// SPI Functions
//==============================================================================

void spi_init(uint32_t speed);
void spi_set_speed(uint32_t speed);
uint8_t spi_transfer(uint8_t data);
void spi_burst_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t count);
void spi_cs_assert(void);
void spi_cs_deassert(void);

#endif // IO_H
