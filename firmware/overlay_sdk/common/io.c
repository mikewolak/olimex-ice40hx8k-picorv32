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

// Standard C library putchar (for bare-metal overlays)
int putchar(int c) {
    uart_putc((char)c);
    return c;
}

// Standard C library exit (for bare-metal overlays)
// Simply returns to caller (SD Card Manager)
void exit(int status) {
    (void)status;  // Unused in bare-metal
    // Just return - overlay will cleanly return to SD Card Manager
    return;
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

//==============================================================================
// Newlib Syscall Stubs (required for printf/malloc support)
//==============================================================================

#include <sys/stat.h>
#include <errno.h>

// Write to file descriptor (1 = stdout, 2 = stderr)
// This is what printf() calls to output data
int _write(int file, char *ptr, int len) {
    // File descriptor 1 (stdout) and 2 (stderr) both go to UART
    if (file == 1 || file == 2) {
        for (int i = 0; i < len; i++) {
            uart_putc(ptr[i]);
        }
        return len;
    }

    // Other file descriptors are not supported
    errno = EBADF;
    return -1;
}

// Read from file descriptor (0 = stdin)
int _read(int file, char *ptr, int len) {
    if (file == 0) {
        // stdin - read from UART
        for (int i = 0; i < len; i++) {
            ptr[i] = uart_getc();
        }
        return len;
    }

    errno = EBADF;
    return -1;
}

// Close file descriptor (not supported)
int _close(int file) {
    (void)file;
    return -1;
}

// Seek in file (not supported)
int _lseek(int file, int offset, int whence) {
    (void)file;
    (void)offset;
    (void)whence;
    return -1;
}

// Get file status (minimal implementation for stdin/stdout/stderr)
int _fstat(int file, struct stat *st) {
    if (file >= 0 && file <= 2) {
        // stdin/stdout/stderr are character devices
        st->st_mode = S_IFCHR;
        return 0;
    }

    errno = EBADF;
    return -1;
}

// Check if file descriptor is a terminal
int _isatty(int file) {
    // stdin/stdout/stderr are all connected to UART (a terminal)
    if (file >= 0 && file <= 2) {
        return 1;
    }
    return 0;
}

// Heap management (sbrk) - for malloc/free support
// Uses overlay heap addresses from memory_config.h
#include "memory_config.h"

static char *heap_ptr = (char *)OVERLAY_HEAP_BASE;

void *_sbrk(int incr) {
    char *prev_heap = heap_ptr;
    char *new_heap = heap_ptr + incr;

    // Check for heap overflow
    if (new_heap >= (char *)OVERLAY_HEAP_END) {
        errno = ENOMEM;  // Out of memory
        return (void *)-1;
    }

    heap_ptr = new_heap;
    return (void *)prev_heap;
}

// Get process ID (not really meaningful in bare metal)
int _getpid(void) {
    return 1;  // Always return 1 (single "process")
}

// Send signal to process (not supported in bare metal)
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;  // Invalid argument
    return -1;
}
