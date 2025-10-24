//==============================================================================
// Olimex iCE40HX8K-EVB RISC-V Platform
// spi_test.c - Interactive SPI Master Peripheral Test Suite (with curses UI)
//
// Copyright (c) October 2025 Michael Wolak
// Email: mikewolak@gmail.com, mike@epromfoundry.com
//
// NOT FOR COMMERCIAL USE
// Educational and research purposes only
//==============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../lib/incurses/curses.h"

// UART MMIO Registers
#define UART_TX_DATA   (*(volatile uint32_t*)0x80000000)
#define UART_TX_STATUS (*(volatile uint32_t*)0x80000004)
#define UART_RX_DATA   (*(volatile uint32_t*)0x80000008)
#define UART_RX_STATUS (*(volatile uint32_t*)0x8000000C)

// Test IDs
#define TEST_REGISTER_DUMP  0
#define TEST_LOOPBACK       1
#define TEST_SPEED_TEST     2
#define TEST_SD_INIT        3
#define TEST_MANUAL_XFER    4
#define TEST_SPI_TERMINAL   5
#define NUM_TESTS           6

// SPI MMIO Registers
#define SPI_CTRL   (*(volatile uint32_t*)0x80000050)
#define SPI_DATA   (*(volatile uint32_t*)0x80000054)
#define SPI_STATUS (*(volatile uint32_t*)0x80000058)
#define SPI_CS     (*(volatile uint32_t*)0x8000005C)

// SPI Status bits
#define SPI_STATUS_BUSY (1 << 0)
#define SPI_STATUS_DONE (1 << 1)

// SPI Clock divider values
#define SPI_CLK_50MHZ   (0 << 2)  // 000 = 50 MHz
#define SPI_CLK_25MHZ   (1 << 2)  // 001 = 25 MHz
#define SPI_CLK_12MHZ   (2 << 2)  // 010 = 12.5 MHz
#define SPI_CLK_6MHZ    (3 << 2)  // 011 = 6.25 MHz
#define SPI_CLK_3MHZ    (4 << 2)  // 100 = 3.125 MHz
#define SPI_CLK_1MHZ    (5 << 2)  // 101 = 1.562 MHz
#define SPI_CLK_781KHZ  (6 << 2)  // 110 = 781 kHz
#define SPI_CLK_390KHZ  (7 << 2)  // 111 = 390 kHz

// Timer peripheral registers (base 0x80000020)
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

// UART functions
void uart_putc(char c) {
    while (UART_TX_STATUS & 1);  // Wait while busy
    UART_TX_DATA = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_puthex(uint32_t val, int digits) {
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        uint32_t nibble = (val >> i) & 0xF;
        uart_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

int uart_getc_available(void) {
    return UART_RX_STATUS & 1;
}

char uart_getc(void) {
    while (!uart_getc_available());
    return UART_RX_DATA & 0xFF;
}

//==============================================================================
// PicoRV32 Custom IRQ Instructions (inline assembly macros)
//==============================================================================

// Enable all interrupts (clear IRQ mask)
static inline void irq_enable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(0));
}

// Disable all interrupts (set IRQ mask to all 1s)
static inline void irq_disable(void) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(~0));
}

// Set specific IRQ mask (0=enabled, 1=disabled)
static inline void irq_setmask(uint32_t mask) {
    uint32_t dummy;
    __asm__ volatile (".insn r 0x0B, 6, 3, %0, %1, x0" : "=r"(dummy) : "r"(mask));
}

//==============================================================================
// Timer Helper Functions
//==============================================================================

static void timer_init(void) {
    TIMER_CR = 0;               // Disable timer
    TIMER_SR = TIMER_SR_UIF;    // Clear any pending interrupt
}

static void timer_config(uint16_t psc, uint32_t arr) {
    TIMER_PSC = psc;
    TIMER_ARR = arr;
}

static void timer_start(void) {
    TIMER_CR = TIMER_CR_ENABLE;  // Enable, continuous mode
}

static void timer_stop(void) {
    TIMER_CR = 0;  // Disable timer
}

static void timer_clear_irq(void) {
    TIMER_SR = TIMER_SR_UIF;     // Write 1 to clear
}

//==============================================================================
// SPI Interrupt Support and Performance Tracking
//==============================================================================

// Global state for interrupt-driven SPI
volatile uint8_t spi_transfer_complete = 0;
volatile uint8_t spi_rx_data_irq = 0;
volatile uint32_t spi_irq_count = 0;  // Track total interrupts fired

// Performance tracking for continuous mode
volatile uint32_t bytes_transferred_this_period = 0;  // Bytes since last timer tick
volatile uint32_t bytes_per_second = 0;               // Calculated bytes/sec
volatile uint8_t timer_tick_flag = 0;                 // Set by timer interrupt
volatile uint32_t timer_tick_counter = 0;             // Counts timer ticks (for multi-tick averaging)

// Interrupt handler (called from start.S when IRQ occurs)
void irq_handler(uint32_t irqs) {
    // Check if Timer interrupt (IRQ[0])
    if (irqs & (1 << 0)) {
        // CRITICAL: Clear the interrupt source FIRST
        timer_clear_irq();

        // Increment timer tick counter
        timer_tick_counter++;

        // Calculate bytes per second (every 10 ticks = 1 second at 10Hz timer)
        // For smoother updates, we calculate every tick (0.1 second resolution at 10Hz)
        if (timer_tick_counter >= 10) {
            // Update bytes_per_second (average over last second)
            bytes_per_second = bytes_transferred_this_period;

            // Reset for next measurement period
            bytes_transferred_this_period = 0;
            timer_tick_counter = 0;
        }

        // Set flag to notify main loop
        timer_tick_flag = 1;
    }

    // Check if SPI interrupt (IRQ[2])
    if (irqs & (1 << 2)) {
        // Increment interrupt counter
        spi_irq_count++;

        // Read received data
        spi_rx_data_irq = (uint8_t)SPI_DATA;

        // Clear DONE flag by reading STATUS
        (void)SPI_STATUS;

        // Set completion flag
        spi_transfer_complete = 1;
    }
}

// Format bytes/sec with auto-adjusting units (B/s, KB/s, MB/s)
void format_bytes_per_sec(uint32_t bytes_per_sec, char *buf, int buf_size) {
    if (bytes_per_sec >= 1000000) {
        // MB/s (1,000,000+ bytes/sec)
        uint32_t mb = bytes_per_sec / 1000000;
        uint32_t frac = (bytes_per_sec % 1000000) / 100000;  // One decimal place
        snprintf(buf, buf_size, "%u.%u MB/s", (unsigned int)mb, (unsigned int)frac);
    } else if (bytes_per_sec >= 1000) {
        // KB/s (1,000+ bytes/sec)
        uint32_t kb = bytes_per_sec / 1000;
        uint32_t frac = (bytes_per_sec % 1000) / 100;  // One decimal place
        snprintf(buf, buf_size, "%u.%u KB/s", (unsigned int)kb, (unsigned int)frac);
    } else {
        // B/s (< 1000 bytes/sec)
        snprintf(buf, buf_size, "%u B/s", (unsigned int)bytes_per_sec);
    }
}

// Manual transfer configuration
typedef struct {
    uint32_t clk_div;      // Clock divider (SPI_CLK_xxx)
    int count;             // Number of times to send same data
    int cpol;              // Clock polarity (0 or 1)
    int cpha;              // Clock phase (0 or 1)
    int irq_mode;          // Interrupt mode (0=polling, 1=interrupt)
    int continuous;        // Continuous mode (0=single, 1=continuous)
} manual_config_t;

// Default manual transfer config
manual_config_t manual_config = {
    .clk_div = SPI_CLK_390KHZ,
    .count = 1,
    .cpol = 0,
    .cpha = 0,
    .irq_mode = 0,
    .continuous = 0
};

// SPI functions
void spi_init(uint32_t clk_div) {
    // Configure: Mode 0 (CPOL=0, CPHA=0), specified clock divider
    SPI_CTRL = clk_div | (0 << 1) | (0 << 0);

    // CS inactive (high)
    SPI_CS = 1;
}

void spi_init_full(uint32_t clk_div, int cpol, int cpha) {
    // Configure: specified mode and clock divider
    SPI_CTRL = clk_div | ((cpha & 1) << 1) | ((cpol & 1) << 0);

    // CS inactive (high)
    SPI_CS = 1;
}

// SPI Transfer - Polling Mode (original method)
uint8_t spi_transfer_polling(uint8_t data) {
    // Wait if busy
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Initiate transfer
    SPI_DATA = data;

    // Wait for completion
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Read status to clear DONE flag
    (void)SPI_STATUS;

    // Read received data
    return (uint8_t)SPI_DATA;
}

// SPI Transfer - Interrupt Mode (non-blocking)
uint8_t spi_transfer_irq(uint8_t data) {
    // Wait if busy (shouldn't happen if used correctly)
    while (SPI_STATUS & SPI_STATUS_BUSY);

    // Clear completion flag
    spi_transfer_complete = 0;

    // Initiate transfer (this will generate IRQ when done)
    SPI_DATA = data;

    // Wait for interrupt to signal completion
    while (!spi_transfer_complete);

    // Return received data (captured in IRQ handler)
    return spi_rx_data_irq;
}

// Mode-switchable wrapper (uses global use_irq_mode flag)
static int use_irq_mode = 0;  // 0=polling, 1=interrupt

uint8_t spi_transfer(uint8_t data) {
    if (use_irq_mode) {
        return spi_transfer_irq(data);
    } else {
        return spi_transfer_polling(data);
    }
}

// Test configuration structure
typedef struct {
    int loopback_iterations;
    int loopback_continuous;
    int loopback_bytes;        // Bytes per transfer in loopback test
    int speed_test_bytes;
    int speed_test_continuous;
    uint32_t speed_test_clock;
} test_config_t;

// Global test config
test_config_t config = {
    .loopback_iterations = 8,
    .loopback_continuous = 0,
    .loopback_bytes = 64,      // Default: 64 bytes per transfer
    .speed_test_bytes = 256,   // Default: 256 bytes per transfer
    .speed_test_continuous = 0,
    .speed_test_clock = SPI_CLK_390KHZ
};

// Test result structure
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    uint32_t last_rx_data;
    char status_msg[80];
} test_result_t;

test_result_t result = {0};

// Draw register dump section
void draw_registers(int start_row) {
    move(start_row, 0);
    attron(A_REVERSE);
    addstr("[ SPI Registers ]");
    standend();

    move(start_row + 1, 2);
    char buf[80];
    snprintf(buf, sizeof(buf), "CTRL:   0x%08X  DATA:   0x%08X",
             (unsigned int)SPI_CTRL, (unsigned int)SPI_DATA);
    addstr(buf);

    move(start_row + 2, 2);
    snprintf(buf, sizeof(buf), "STATUS: 0x%08X  CS:     0x%08X",
             (unsigned int)SPI_STATUS, (unsigned int)SPI_CS);
    addstr(buf);
}

// Show help screen
void show_help(void) {
    clear();

    int row = 0;

    // Title
    move(row++, 0);
    attron(A_REVERSE);
    addstr("Interactive SPI Test Suite - Help");
    for (int i = 34; i < COLS; i++) addch(' ');
    standend();

    row++;
    move(row++, 0);
    attron(A_REVERSE);
    addstr("KEYBOARD CONTROLS");
    standend();

    move(row++, 2);
    addstr("Arrow Up/Down, j/k  : Navigate between tests");
    move(row++, 2);
    addstr("Enter               : Run selected test");
    move(row++, 2);
    addstr("E                   : Edit test parameters (toggles modes/values)");
    move(row++, 2);
    addstr("I                   : Toggle SPI mode (Polling vs Interrupt-driven)");
    move(row++, 2);
    addstr("Space               : Stop running test (during continuous mode)");
    move(row++, 2);
    addstr("H                   : Show this help screen");
    move(row++, 2);
    addstr("Q                   : Quit application");

    row++;
    move(row++, 0);
    attron(A_REVERSE);
    addstr("HARDWARE SETUP - SPI PINOUT");
    standend();

    move(row++, 2);
    addstr("SPI Master Peripheral @ 0x80000050 (FPGA GPIO Pins):");
    row++;
    move(row++, 2);
    addstr("Pin B1  : MOSI (Master Out, Slave In)  - SPI Data Output");
    move(row++, 2);
    addstr("Pin C1  : MISO (Master In, Slave Out)  - SPI Data Input");
    move(row++, 2);
    addstr("Pin A2  : SCLK (Serial Clock)          - SPI Clock Output");
    move(row++, 2);
    addstr("Pin B2  : CS   (Chip Select)           - SPI Chip Select (active low)");

    row++;
    move(row++, 0);
    attron(A_REVERSE);
    addstr("TEST DESCRIPTIONS");
    standend();

    move(row++, 2);
    attron(A_REVERSE);
    addstr("1. Loopback Test");
    standend();
    move(row++, 4);
    addstr("Purpose: Verify SPI transmit and receive functionality");
    move(row++, 4);
    addstr("Setup:   Connect MOSI (B1) to MISO (C1) with jumper wire");
    move(row++, 4);
    addstr("Action:  Sends 8 test patterns and verifies RX matches TX");
    move(row++, 4);
    addstr("Config:  Press E to toggle Fixed/Continuous mode");
    move(row++, 4);
    addstr("         In Fixed mode, press E to cycle iterations (8/16/32/64)");

    row++;
    move(row++, 2);
    attron(A_REVERSE);
    addstr("2. Speed Test");
    standend();
    move(row++, 4);
    addstr("Purpose: Test all SPI clock speeds (390kHz to 50MHz)");
    move(row++, 4);
    addstr("Setup:   Optional - connect logic analyzer to observe signals");
    move(row++, 4);
    addstr("Action:  Transfers data at each of 8 clock speeds");
    move(row++, 4);
    addstr("Config:  Press E to toggle Single/Continuous mode");
    move(row++, 4);
    addstr("         In Single mode, press E to cycle bytes (100/256/512/1024)");

    row++;
    move(row++, 2);
    attron(A_REVERSE);
    addstr("3. SD Card Init Pattern");
    standend();
    move(row++, 4);
    addstr("Purpose: Test SD card initialization sequence");
    move(row++, 4);
    addstr("Setup:   Connect SD card module (or observe with scope/analyzer)");
    move(row++, 4);
    addstr("Action:  Sends proper SD init: 80 clocks, CMD0, reads R1 response");
    move(row++, 4);
    addstr("Result:  R1=0x01 indicates SD card detected and in idle state");

    row++;
    move(row++, 2);
    attron(A_REVERSE);
    addstr("4. Manual Transfer");
    standend();
    move(row++, 4);
    addstr("Purpose: Send single SPI bytes with full control");
    move(row++, 4);
    addstr("Setup:   Connect your SPI device");
    move(row++, 4);
    addstr("Action:  Type 1-2 hex digits, press Enter to send, see RX response");
    move(row++, 4);
    addstr("Example: Type 'A5' or 'F' (auto-pads to 0x0F) or \"Hi\" for ASCII");
    move(row++, 4);
    addstr("Control: T toggles CS, Backspace edits, ESC exits");
    move(row++, 4);
    addstr("Use:     Quick single-byte testing and simple protocols");

    row++;
    move(row++, 2);
    attron(A_REVERSE);
    addstr("5. SPI Terminal (Interactive)");
    standend();
    move(row++, 4);
    addstr("Purpose: Interactive hex command terminal with transaction history");
    move(row++, 4);
    addstr("Setup:   Connect your SPI device");
    move(row++, 4);
    addstr("Action:  Type hex digits (with or without spaces), press Enter to send");
    move(row++, 4);
    addstr("Example: 'ABCD' or 'AB CD' sends 2 bytes | \"Hi\" sends ASCII | Max 16 bytes");
    move(row++, 4);
    addstr("Control: T toggles CS, Backspace edits, ESC exits to menu");
    move(row++, 4);
    addstr("History: Last 10 transactions displayed (scrolling)");
    move(row++, 4);
    addstr("Use:     Perfect for device exploration and debugging");

    // Status bar
    move(LINES - 1, 0);
    attron(A_REVERSE);
    addstr("Press any key to return to main menu");
    for (int i = 37; i < COLS; i++) addch(' ');
    standend();

    refresh();

    // Wait for ESC or Enter only - ignore other keys (including arrow keys)
    flushinp();
    timeout(-1);
    while (1) {
        int key = getch();
        if (key == 27 || key == '\n' || key == '\r') {
            break;  // ESC or Enter closes help
        }
        // Ignore all other keys (including arrow keys)
    }
}

// Run loopback test
void run_loopback_test(int result_row, int *stop) {
    uint8_t test_patterns[] = {0x11, 0xFF, 0xAA, 0x55, 0x12, 0x34, 0x56, 0x78};
    char buf[80];
    int row = result_row;

    // Initialize SPI to a reasonable default speed (12.5 MHz)
    spi_init(SPI_CLK_12MHZ);

    result.total_tests = 0;
    result.passed_tests = 0;
    result.failed_tests = 0;

    int iterations = config.loopback_continuous ? 999999 : config.loopback_iterations;

    // If continuous mode, enable timer for performance measurement
    if (config.loopback_continuous) {
        // Reset performance counters
        bytes_transferred_this_period = 0;
        bytes_per_second = 0;
        timer_tick_counter = 0;
        timer_tick_flag = 0;

        // Configure timer for 10 Hz (100ms period)
        // System clock: 50 MHz
        // Prescaler: 49 (divide by 50) → 1 MHz tick rate
        // Auto-reload: 99999 → 1,000,000 / 100,000 = 10 Hz
        timer_init();
        timer_config(49, 99999);

        // Enable both Timer (IRQ[0]) and SPI (IRQ[2]) if in IRQ mode
        if (use_irq_mode) {
            irq_setmask(~((1 << 0) | (1 << 2)));  // Enable Timer + SPI
        } else {
            irq_setmask(~(1 << 0));  // Enable Timer only
        }

        timer_start();
    }

    for (int iter = 0; iter < iterations && !(*stop); iter++) {
        SPI_CS = 0;

        // Send config.loopback_bytes per iteration
        for (int i = 0; i < config.loopback_bytes; i++) {
            // Cycle through test patterns
            uint8_t tx = test_patterns[i % 8];
            uint8_t rx = spi_transfer(tx);

            // Track bytes for performance measurement
            if (config.loopback_continuous) {
                bytes_transferred_this_period++;
            }

            // Only update test result display in non-continuous mode
            // (In continuous mode, only show performance updates)
            // Show first 8 bytes only to avoid screen overflow
            if (!config.loopback_continuous && i < 8) {
                move(row + i, 0);
                clrtoeol();
                snprintf(buf, sizeof(buf), "  [%04d] TX: 0x%02X -> RX: 0x%02X ",
                         iter + 1, tx, rx);
                addstr(buf);

                result.total_tests++;
                if (tx == rx) {
                    attron(A_REVERSE);
                    addstr("[PASS]");
                    standend();
                    result.passed_tests++;
                } else {
                    attron(A_UNDERLINE);
                    addstr("[FAIL]");
                    standend();
                    result.failed_tests++;
                }
            } else {
                // In continuous mode or beyond first 8 bytes, still track pass/fail but don't display
                result.total_tests++;
                if (tx == rx) {
                    result.passed_tests++;
                } else {
                    result.failed_tests++;
                }
            }

            // Check for stop key every 64 bytes to avoid overhead
            if ((i & 0x3F) == 0) {
                timeout(0);
                int ch = getch();
                if (ch == ' ') *stop = 1;
                timeout(-1);
            }
        }

        SPI_CS = 1;

        // Update performance display if continuous mode and timer ticked
        if (config.loopback_continuous && timer_tick_flag) {
            timer_tick_flag = 0;
            move(row + 9, 0);
            clrtoeol();
            char perf_buf[40];
            format_bytes_per_sec(bytes_per_second, perf_buf, sizeof(perf_buf));
            snprintf(buf, sizeof(buf), "  Performance: %s | SPI IRQ: %u",
                     perf_buf, (unsigned int)spi_irq_count);
            addstr(buf);
            refresh();  // Only refresh when performance updates
        } else if (!config.loopback_continuous) {
            refresh();  // Refresh after each iteration in non-continuous mode
        }
    }

    // Stop timer if continuous mode
    if (config.loopback_continuous) {
        timer_stop();

        // Restore IRQ mask to just SPI if in IRQ mode, or disable all
        if (use_irq_mode) {
            irq_setmask(~(1 << 2));  // Enable SPI only
        } else {
            irq_disable();
        }
    }

    // Summary
    move(row + 9, 0);
    clrtoeol();
    if (config.loopback_continuous) {
        char perf_buf[40];
        format_bytes_per_sec(bytes_per_second, perf_buf, sizeof(perf_buf));
        snprintf(buf, sizeof(buf), "  Final: %d passed, %d failed | %s",
                 result.passed_tests, result.failed_tests, perf_buf);
    } else {
        snprintf(buf, sizeof(buf), "  Results: %d passed, %d failed (%.1f%% pass rate)",
                 result.passed_tests, result.failed_tests,
                 result.total_tests > 0 ? (100.0 * result.passed_tests / result.total_tests) : 0);
    }
    addstr(buf);
    refresh();
}

// Run speed test
void run_speed_test(int result_row, int *stop) {
    const char *speed_names[] = {
        "50.0 MHz", "25.0 MHz", "12.5 MHz", "6.25 MHz",
        "3.125 MHz", "1.562 MHz", "781 kHz", "390 kHz"
    };
    uint32_t speeds[] = {
        SPI_CLK_50MHZ, SPI_CLK_25MHZ, SPI_CLK_12MHZ, SPI_CLK_6MHZ,
        SPI_CLK_3MHZ, SPI_CLK_1MHZ, SPI_CLK_781KHZ, SPI_CLK_390KHZ
    };

    char buf[80];
    int iterations = config.speed_test_continuous ? 999999 : 1;

    // If continuous mode, enable timer for performance measurement
    if (config.speed_test_continuous) {
        // Reset performance counters
        bytes_transferred_this_period = 0;
        bytes_per_second = 0;
        timer_tick_counter = 0;
        timer_tick_flag = 0;

        // Configure timer for 10 Hz (100ms period)
        timer_init();
        timer_config(49, 99999);

        // Enable both Timer (IRQ[0]) and SPI (IRQ[2]) if in IRQ mode
        if (use_irq_mode) {
            irq_setmask(~((1 << 0) | (1 << 2)));  // Enable Timer + SPI
        } else {
            irq_setmask(~(1 << 0));  // Enable Timer only
        }

        timer_start();
    }

    for (int iter = 0; iter < iterations && !(*stop); iter++) {
        for (int i = 0; i < 8; i++) {
            // Only update per-speed status in non-continuous mode
            if (!config.speed_test_continuous) {
                move(result_row + i, 0);
                clrtoeol();
                snprintf(buf, sizeof(buf), "  [%04d] %-10s... ", iter + 1, speed_names[i]);
                addstr(buf);
                refresh();
            }

            spi_init(speeds[i]);
            SPI_CS = 0;
            for (int j = 0; j < config.speed_test_bytes; j++) {
                spi_transfer(j & 0xFF);

                // Track bytes for performance measurement
                if (config.speed_test_continuous) {
                    bytes_transferred_this_period++;
                }
            }
            SPI_CS = 1;

            // Only show per-speed results in non-continuous mode
            if (!config.speed_test_continuous) {
                attron(A_REVERSE);
                snprintf(buf, sizeof(buf), "OK (%d bytes)", config.speed_test_bytes);
                addstr(buf);
                standend();
            }

            // Check for stop key
            timeout(0);
            int ch = getch();
            if (ch == ' ') *stop = 1;
            timeout(-1);

            // Update performance display if continuous mode and timer ticked
            if (config.speed_test_continuous && timer_tick_flag) {
                timer_tick_flag = 0;
                move(result_row + 9, 0);
                clrtoeol();
                char perf_buf[40];
                format_bytes_per_sec(bytes_per_second, perf_buf, sizeof(perf_buf));
                snprintf(buf, sizeof(buf), "  Overall Performance: %s | SPI IRQ: %u",
                         perf_buf, (unsigned int)spi_irq_count);
                addstr(buf);
                refresh();  // Only refresh when performance updates
            } else if (!config.speed_test_continuous) {
                refresh();  // Refresh after each speed in non-continuous mode
            }
        }
    }

    // Stop timer if continuous mode
    if (config.speed_test_continuous) {
        timer_stop();

        // Restore IRQ mask to just SPI if in IRQ mode, or disable all
        if (use_irq_mode) {
            irq_setmask(~(1 << 2));  // Enable SPI only
        } else {
            irq_disable();
        }

        // Final performance summary
        move(result_row + 9, 0);
        clrtoeol();
        char perf_buf[40];
        format_bytes_per_sec(bytes_per_second, perf_buf, sizeof(perf_buf));
        snprintf(buf, sizeof(buf), "  Final Performance: %s | Total SPI IRQ: %u",
                 perf_buf, (unsigned int)spi_irq_count);
        addstr(buf);
        refresh();
    }
}

// Run SD card init test
void run_sd_init_test(int result_row, int *stop) {
    char buf[80];

    spi_init(SPI_CLK_390KHZ);
    move(result_row, 0);
    addstr("  1. Set clock to 390 kHz");
    refresh();

    if (*stop) return;

    move(result_row + 1, 0);
    addstr("  2. Sending 80 dummy clocks... ");
    refresh();
    SPI_CS = 1;
    for (int i = 0; i < 10; i++) {
        spi_transfer(0xFF);
    }
    addstr("Done");
    refresh();

    if (*stop) return;

    move(result_row + 2, 0);
    addstr("  3. Sending CMD0 (GO_IDLE)... ");
    refresh();
    SPI_CS = 0;
    spi_transfer(0x40);
    spi_transfer(0x00);
    spi_transfer(0x00);
    spi_transfer(0x00);
    spi_transfer(0x00);
    spi_transfer(0x95);
    addstr("Done");
    refresh();

    move(result_row + 3, 0);
    uint8_t r1 = spi_transfer(0xFF);
    snprintf(buf, sizeof(buf), "  4. R1 response: 0x%02X %s", r1,
             r1 == 0x01 ? "(idle state - card present!)" : "(no card detected)");
    addstr(buf);
    SPI_CS = 1;
    refresh();
}

// Show configuration menu for manual transfer
void show_manual_config_menu(void) {
    const char *speed_names[] = {
        "50.0 MHz", "25.0 MHz", "12.5 MHz", "6.25 MHz",
        "3.125 MHz", "1.562 MHz", "781 kHz", "390 kHz"
    };
    uint32_t speeds[] = {
        SPI_CLK_50MHZ, SPI_CLK_25MHZ, SPI_CLK_12MHZ, SPI_CLK_6MHZ,
        SPI_CLK_3MHZ, SPI_CLK_1MHZ, SPI_CLK_781KHZ, SPI_CLK_390KHZ
    };

    int selected = 0;
    int old_selected = -1;
    int num_options = 6;  // Added continuous mode option
    int need_redraw = 1;

    // Flush any pending input
    flushinp();

    while (1) {
        // Only redraw when needed
        if (need_redraw || old_selected != selected) {
            // Draw popup box
            int box_width = 50;
            int box_height = 16;  // Increased for continuous mode option
            int start_row = (LINES - box_height) / 2;
            int start_col = (COLS - box_width) / 2;

            // Clear and draw border
            for (int r = start_row; r < start_row + box_height; r++) {
                move(r, start_col);
                for (int c = 0; c < box_width; c++) {
                    if (r == start_row || r == start_row + box_height - 1) {
                        addch('-');
                    } else if (c == 0 || c == box_width - 1) {
                        addch('|');
                    } else {
                        addch(' ');
                    }
                }
            }

            // Title
            move(start_row + 1, start_col + 2);
            attron(A_REVERSE);
            addstr(" Manual Transfer Configuration ");
            standend();

            // Find current speed index
            int speed_idx = 7;  // Default to 390kHz
            for (int i = 0; i < 8; i++) {
                if (speeds[i] == manual_config.clk_div) {
                    speed_idx = i;
                    break;
                }
            }

            // Menu options
            char buf[60];
            int row = start_row + 3;

            // Speed
            move(row++, start_col + 2);
            if (selected == 0) attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "Speed:     %s", speed_names[speed_idx]);
            addstr(buf);
            if (selected == 0) standend();
            clrtoeol();

            // Count
            move(row++, start_col + 2);
            if (selected == 1) attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "Count:     %d", manual_config.count);
            addstr(buf);
            if (selected == 1) standend();
            clrtoeol();

            // CPOL
            move(row++, start_col + 2);
            if (selected == 2) attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "CPOL:      %d (Clock polarity)", manual_config.cpol);
            addstr(buf);
            if (selected == 2) standend();
            clrtoeol();

            // CPHA
            move(row++, start_col + 2);
            if (selected == 3) attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "CPHA:      %d (Clock phase)", manual_config.cpha);
            addstr(buf);
            if (selected == 3) standend();
            clrtoeol();

            // IRQ Mode
            move(row++, start_col + 2);
            if (selected == 4) attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "IRQ Mode:  %s", manual_config.irq_mode ? "Interrupt" : "Polling");
            addstr(buf);
            if (selected == 4) standend();
            clrtoeol();

            // Continuous Mode
            move(row++, start_col + 2);
            if (selected == 5) attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "Mode:      %s", manual_config.continuous ? "Continuous" : "Single");
            addstr(buf);
            if (selected == 5) standend();
            clrtoeol();

            // Instructions
            row += 2;
            move(row++, start_col + 2);
            addstr("Up/Down: Navigate | Left/Right: Change");
            move(row++, start_col + 2);
            addstr("Enter: Done | ESC: Cancel");

            refresh();

            need_redraw = 0;
            old_selected = selected;
        }

        // Get input
        timeout(-1);
        int ch = getch();

        if (ch == 27) {  // ESC or arrow key
            // Check if this is an arrow key (ESC [ X) or just ESC
            timeout(10);  // Brief timeout to check for following characters
            int ch2 = getch();
            if (ch2 == '[') {
                int ch3 = getch();
                timeout(-1);
                if (ch3 == 'A') ch = KEY_UP;
                else if (ch3 == 'B') ch = KEY_DOWN;
                else if (ch3 == 'C') ch = KEY_RIGHT;
                else if (ch3 == 'D') ch = KEY_LEFT;
                else {
                    break;  // Unknown escape sequence, treat as ESC
                }
            } else {
                timeout(-1);
                // Either just ESC or ESC + some other character
                // In either case, treat as ESC and cancel
                break;
            }
        }

        if (ch == '\n' || ch == '\r') {  // Enter - accept
            // Apply configuration
            spi_init_full(manual_config.clk_div, manual_config.cpol, manual_config.cpha);

            // Set IRQ mode
            use_irq_mode = manual_config.irq_mode;
            if (use_irq_mode) {
                irq_setmask(~(1 << 2));  // Enable SPI IRQ
            } else {
                irq_disable();
            }
            break;
        }
        else if (ch == 'k' || ch == KEY_UP) {  // Up
            selected = (selected - 1 + num_options) % num_options;
        }
        else if (ch == 'j' || ch == KEY_DOWN) {  // Down
            selected = (selected + 1) % num_options;
        }
        else if (ch == 'l' || ch == KEY_RIGHT) {  // Right - increment
            if (selected == 0) {  // Speed
                // Find current speed index
                int speed_idx = 7;
                for (int i = 0; i < 8; i++) {
                    if (speeds[i] == manual_config.clk_div) {
                        speed_idx = i;
                        break;
                    }
                }
                speed_idx = (speed_idx + 1) % 8;
                manual_config.clk_div = speeds[speed_idx];
            }
            else if (selected == 1) {  // Count
                if (manual_config.count < 100) manual_config.count++;
            }
            else if (selected == 2) {  // CPOL
                manual_config.cpol = !manual_config.cpol;
            }
            else if (selected == 3) {  // CPHA
                manual_config.cpha = !manual_config.cpha;
            }
            else if (selected == 4) {  // IRQ Mode
                manual_config.irq_mode = !manual_config.irq_mode;
            }
            else if (selected == 5) {  // Continuous Mode
                manual_config.continuous = !manual_config.continuous;
            }
            need_redraw = 1;  // Trigger screen update
        }
        else if (ch == 'h' || ch == KEY_LEFT) {  // Left - decrement
            if (selected == 0) {  // Speed
                // Find current speed index
                int speed_idx = 7;
                for (int i = 0; i < 8; i++) {
                    if (speeds[i] == manual_config.clk_div) {
                        speed_idx = i;
                        break;
                    }
                }
                speed_idx = (speed_idx - 1 + 8) % 8;
                manual_config.clk_div = speeds[speed_idx];
            }
            else if (selected == 1) {  // Count
                if (manual_config.count > 1) manual_config.count--;
            }
            else if (selected == 2) {  // CPOL
                manual_config.cpol = !manual_config.cpol;
            }
            else if (selected == 3) {  // CPHA
                manual_config.cpha = !manual_config.cpha;
            }
            else if (selected == 4) {  // IRQ Mode
                manual_config.irq_mode = !manual_config.irq_mode;
            }
            else if (selected == 5) {  // Continuous Mode
                manual_config.continuous = !manual_config.continuous;
            }
            need_redraw = 1;  // Trigger screen update
        }
    }
}

// Run manual transfer with hex input (like hexedit's goto)
void run_manual_transfer(void) {
    char input_buf[64] = {0};
    int input_pos = 0;
    int cs_state = 1;  // Start with CS high
    uint8_t tx_bytes[16];
    uint8_t rx_bytes[16];
    int last_byte_count = 0;
    int need_full_redraw = 1;
    int need_config_update = 0;
    int need_cs_update = 0;
    int need_input_update = 0;
    int need_result_update = 0;
    int need_perf_update = 0;
    int timer_running = 0;  // Track if timer is currently running

    // Apply manual_config SPI settings on entry
    spi_init_full(manual_config.clk_div, manual_config.cpol, manual_config.cpha);

    clear();

    while (1) {
        // Full redraw (only on first time or after config menu)
        if (need_full_redraw) {
            clear();

            // Header
            move(0, 0);
            attron(A_REVERSE);
            addstr("Manual SPI Transfer - Type hex/\"ASCII\" | E:Config | T:CS | ESC:Exit");
            for (int i = 72; i < COLS; i++) addch(' ');
            standend();

            // Input prompt header
            move(5, 0);
            attron(A_REVERSE);
            addstr("[ Enter Bytes (up to 16) ]");
            standend();
            clrtoeol();

            // Last result header
            move(8, 0);
            attron(A_REVERSE);
            addstr("[ Last Transfer ]");
            standend();
            clrtoeol();

            // Performance header (only in continuous mode)
            if (manual_config.continuous) {
                move(11, 0);
                attron(A_REVERSE);
                addstr("[ Performance ]");
                standend();
                clrtoeol();
            }

            // Instructions
            move(14, 0);
            addstr("Type hex digits OR \"quoted ASCII string\" - up to 32 digits (16 bytes)");
            clrtoeol();
            move(15, 0);
            addstr("ENTER: Send | E: Edit config | T: Toggle CS | BACKSPACE: Delete | ESC: Exit");
            clrtoeol();

            // Status bar
            move(LINES - 1, 0);
            attron(A_REVERSE);
            addstr("E:Config | Ex: ABCD | AB CD | \"Hi!\" | \"Mike Wolak\" | Max 16 bytes");
            for (int i = 71; i < COLS; i++) addch(' ');
            standend();

            need_full_redraw = 0;
            need_config_update = 1;
            need_cs_update = 1;
            need_input_update = 1;
            need_result_update = 1;
            need_perf_update = 1;
        }

        // Update config line (only when changed)
        if (need_config_update) {
            move(2, 0);
            char buf[80];
            const char *speed_names[] = {
                "50MHz", "25MHz", "12.5MHz", "6.25MHz",
                "3.125MHz", "1.56MHz", "781kHz", "390kHz"
            };
            uint32_t speeds[] = {
                SPI_CLK_50MHZ, SPI_CLK_25MHZ, SPI_CLK_12MHZ, SPI_CLK_6MHZ,
                SPI_CLK_3MHZ, SPI_CLK_1MHZ, SPI_CLK_781KHZ, SPI_CLK_390KHZ
            };
            int speed_idx = 7;
            for (int i = 0; i < 8; i++) {
                if (speeds[i] == manual_config.clk_div) {
                    speed_idx = i;
                    break;
                }
            }

            snprintf(buf, sizeof(buf), "Config: %s | Count:%d | Mode:%d%d | %s | IRQ Count:%u",
                     speed_names[speed_idx], manual_config.count,
                     manual_config.cpol, manual_config.cpha,
                     manual_config.irq_mode ? "INT" : "POLL",
                     (unsigned int)spi_irq_count);
            addstr(buf);
            clrtoeol();
            need_config_update = 0;
        }

        // Update CS state (only when changed)
        if (need_cs_update) {
            move(3, 0);
            char buf[80];
            snprintf(buf, sizeof(buf), "CS: %s", cs_state ? "INACTIVE (1)" : "ACTIVE (0)");
            addstr(buf);
            clrtoeol();
            need_cs_update = 0;
        }

        // Update input line (only when changed)
        if (need_input_update) {
            move(6, 0);
            addstr("TX (hex): ");
            addstr(input_buf);
            addch('_');  // Cursor
            clrtoeol();
            need_input_update = 0;
        }

        // Update result lines (only when changed)
        if (need_result_update) {
            if (last_byte_count > 0) {
                move(9, 0);
                addstr("TX: ");
                for (int i = 0; i < last_byte_count && i < 16; i++) {
                    char hex[4];
                    snprintf(hex, sizeof(hex), "%02X ", tx_bytes[i]);
                    addstr(hex);
                }
                clrtoeol();

                move(10, 0);
                addstr("RX: ");
                for (int i = 0; i < last_byte_count && i < 16; i++) {
                    char hex[4];
                    snprintf(hex, sizeof(hex), "%02X ", rx_bytes[i]);
                    addstr(hex);
                }
                clrtoeol();
            } else {
                move(9, 0);
                addstr("(no transfer yet)");
                clrtoeol();
                move(10, 0);
                clrtoeol();
            }
            need_result_update = 0;
        }

        // Update performance display (only in continuous mode and when timer ticks)
        if (manual_config.continuous && timer_tick_flag) {
            timer_tick_flag = 0;
            need_perf_update = 1;
        }

        if (manual_config.continuous && need_perf_update) {
            move(12, 0);
            char perf_buf[40];
            format_bytes_per_sec(bytes_per_second, perf_buf, sizeof(perf_buf));
            char buf[80];
            snprintf(buf, sizeof(buf), "Throughput: %s | SPI IRQ: %u",
                     perf_buf, (unsigned int)spi_irq_count);
            addstr(buf);
            clrtoeol();
            need_perf_update = 0;
        }

        refresh();

        // Get input
        timeout(-1);
        int ch = getch();

        // Check if we're in ASCII entry mode (started with quote but not closed)
        int in_ascii_mode = 0;
        if (input_pos > 0 && input_buf[0] == '"') {
            // Check if closing quote exists
            int has_closing_quote = 0;
            for (int i = 1; i < input_pos; i++) {
                if (input_buf[i] == '"') {
                    has_closing_quote = 1;
                    break;
                }
            }
            in_ascii_mode = !has_closing_quote;
        }

        if (ch == 27) {  // ESC
            break;
        }
        else if (!in_ascii_mode && (ch == 'e' || ch == 'E') && input_pos == 0) {  // Edit configuration (only if buffer empty)
            show_manual_config_menu();
            need_full_redraw = 1;  // Full redraw after config menu
        }
        else if (!in_ascii_mode && (ch == 't' || ch == 'T')) {  // Toggle CS
            cs_state = !cs_state;
            SPI_CS = cs_state;
            need_cs_update = 1;
        }
        else if (ch == '\n' || ch == '\r') {  // Send
            if (input_pos > 0) {
                int byte_count = 0;

                // Check if input is a quoted ASCII string
                if (input_buf[0] == '"') {
                    // Find closing quote
                    int end_quote = -1;
                    for (int i = 1; i < input_pos; i++) {
                        if (input_buf[i] == '"') {
                            end_quote = i;
                            break;
                        }
                    }

                    // Extract ASCII string and convert to bytes
                    if (end_quote > 0) {
                        for (int i = 1; i < end_quote && byte_count < 16; i++) {
                            tx_bytes[byte_count++] = (uint8_t)input_buf[i];
                        }
                    }
                } else {
                    // Parse hex input - flexible format (same as terminal)
                    // First pass: extract only hex digits (skip spaces)
                    char hex_only[33] = {0};  // Max 32 nibbles + null
                    int hex_count = 0;

                    for (int i = 0; i < input_pos && hex_count < 32; i++) {
                        char c = input_buf[i];
                        if ((c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F')) {
                            hex_only[hex_count++] = c;
                        }
                    }

                    // Second pass: convert pairs of hex digits to bytes
                    for (int i = 0; i < hex_count && byte_count < 16; i += 2) {
                    uint32_t val = 0;

                    // First nibble
                    char c1 = hex_only[i];
                    if (c1 >= '0' && c1 <= '9')
                        val = (c1 - '0') << 4;
                    else if (c1 >= 'a' && c1 <= 'f')
                        val = (c1 - 'a' + 10) << 4;
                    else if (c1 >= 'A' && c1 <= 'F')
                        val = (c1 - 'A' + 10) << 4;

                    // Second nibble (if present)
                    if (i + 1 < hex_count) {
                        char c2 = hex_only[i + 1];
                        if (c2 >= '0' && c2 <= '9')
                            val |= (c2 - '0');
                        else if (c2 >= 'a' && c2 <= 'f')
                            val |= (c2 - 'a' + 10);
                        else if (c2 >= 'A' && c2 <= 'F')
                            val |= (c2 - 'A' + 10);
                    }

                        tx_bytes[byte_count++] = (uint8_t)val;
                    }
                }

                // Send bytes and receive responses (repeat count times)
                if (byte_count > 0) {
                    // Set IRQ mode for this transfer
                    use_irq_mode = manual_config.irq_mode;

                    last_byte_count = byte_count;

                    // If continuous mode, set up timer and loop until user presses Space or ESC
                    int continuous_stop = 0;

                    // Start timer if entering continuous mode and not already running
                    if (manual_config.continuous && !timer_running) {
                        // Reset performance counters
                        bytes_transferred_this_period = 0;
                        bytes_per_second = 0;
                        timer_tick_counter = 0;
                        timer_tick_flag = 0;

                        // Configure timer for 10 Hz (100ms period)
                        timer_init();
                        timer_config(49, 99999);

                        // Enable both Timer (IRQ[0]) and SPI (IRQ[2]) if in IRQ mode
                        if (manual_config.irq_mode) {
                            irq_setmask(~((1 << 0) | (1 << 2)));  // Enable Timer + SPI
                        } else {
                            irq_setmask(~(1 << 0));  // Enable Timer only
                        }

                        timer_start();
                        timer_running = 1;
                    }

                    do {
                        // Repeat the transfer 'count' times
                        for (int rep = 0; rep < manual_config.count; rep++) {
                            for (int i = 0; i < byte_count; i++) {
                                rx_bytes[i] = spi_transfer(tx_bytes[i]);
                            }
                        }

                        // Track bytes for performance measurement (byte_count * repeat count)
                        if (manual_config.continuous) {
                            bytes_transferred_this_period += (byte_count * manual_config.count);
                        }

                        need_result_update = 1;
                        need_config_update = 1;  // Update IRQ count

                        // Update display if in continuous mode
                        if (manual_config.continuous) {
                            // Update performance display if timer ticked
                            static int perf_blink_state = 0;
                            if (timer_tick_flag) {
                                timer_tick_flag = 0;
                                need_perf_update = 1;
                                perf_blink_state = !perf_blink_state;  // Toggle blink state
                            }

                            if (need_perf_update) {
                                // Blink the Performance header (visual indicator it's running)
                                move(11, 0);
                                if (perf_blink_state) {
                                    attron(A_REVERSE);
                                }
                                addstr("[ Performance ]");
                                if (perf_blink_state) {
                                    standend();
                                }
                                clrtoeol();

                                // Update throughput line
                                move(12, 0);
                                char perf_buf[40];
                                format_bytes_per_sec(bytes_per_second, perf_buf, sizeof(perf_buf));
                                char buf[80];
                                snprintf(buf, sizeof(buf), "Throughput: %s | SPI IRQ: %u",
                                         perf_buf, (unsigned int)spi_irq_count);
                                addstr(buf);
                                clrtoeol();
                                need_perf_update = 0;
                            }

                            refresh();

                            // Check for stop key (Space or ESC)
                            timeout(0);
                            int stop_ch = getch();
                            timeout(-1);
                            if (stop_ch == ' ' || stop_ch == 27) {
                                continuous_stop = 1;
                            }
                        }
                    } while (manual_config.continuous && !continuous_stop);

                    // Stop timer after continuous mode exits
                    if (manual_config.continuous && timer_running && continuous_stop) {
                        timer_stop();
                        timer_running = 0;

                        // Restore IRQ mask
                        if (manual_config.irq_mode) {
                            irq_setmask(~(1 << 2));  // Enable SPI only
                        } else {
                            irq_disable();
                        }
                    }
                }

                // Clear input (only in single mode, keep it in continuous)
                if (!manual_config.continuous) {
                    input_buf[0] = '\0';
                    input_pos = 0;
                    need_input_update = 1;
                }
            }
        }
        else if (ch == 8 || ch == 127) {  // Backspace
            if (input_pos > 0) {
                input_pos--;
                input_buf[input_pos] = '\0';
                need_input_update = 1;
            }
        }
        else if (ch >= ' ' && ch <= '~' && input_pos < 62) {  // Printable characters
            input_buf[input_pos++] = ch;
            input_buf[input_pos] = '\0';
            need_input_update = 1;
        }
    }

    // Cleanup: Stop timer if it was running
    if (timer_running) {
        timer_stop();

        // Restore IRQ mask to just SPI if in IRQ mode, or disable all
        if (manual_config.irq_mode) {
            irq_setmask(~(1 << 2));  // Enable SPI only
        } else {
            irq_disable();
        }
    }
}

// Interactive SPI Terminal
void run_spi_terminal(void) {
    char input_buf[64] = {0};
    int input_pos = 0;
    uint8_t tx_bytes[32];
    uint8_t rx_bytes[32];
    int cs_state = 1;  // Start with CS high (inactive)

    // Transaction history (circular buffer)
    #define MAX_HISTORY 10
    char history[MAX_HISTORY][80];
    int history_count = 0;
    int history_start = 0;

    clear();

    while (1) {
        // Header
        move(0, 0);
        attron(A_REVERSE);
        char header[80];
        snprintf(header, sizeof(header), "SPI Terminal - Type hex/\"ASCII\" | T:Toggle CS | IRQ:%u | ESC:Exit", (unsigned int)spi_irq_count);
        addstr(header);
        for (int i = strlen(header); i < COLS; i++) addch(' ');
        standend();

        // CS State
        move(2, 0);
        char cs_buf[80];
        snprintf(cs_buf, sizeof(cs_buf), "CS: %s", cs_state ? "INACTIVE (1)" : "ACTIVE (0)");
        addstr(cs_buf);
        clrtoeol();

        // Transaction history
        move(4, 0);
        attron(A_REVERSE);
        addstr("[ Transaction History ]");
        standend();
        clrtoeol();

        // Display history (scrolling from bottom)
        for (int i = 0; i < MAX_HISTORY && i < history_count; i++) {
            int idx = (history_start + i) % MAX_HISTORY;
            move(5 + i, 0);
            addstr(history[idx]);
            clrtoeol();
        }

        // Clear remaining history lines
        for (int i = history_count; i < MAX_HISTORY; i++) {
            move(5 + i, 0);
            clrtoeol();
        }

        // Input area
        move(16, 0);
        attron(A_REVERSE);
        addstr("[ Command Input ]");
        standend();
        clrtoeol();

        move(17, 0);
        addstr("TX (hex): ");
        addstr(input_buf);
        addch('_');  // Cursor
        clrtoeol();

        // Instructions
        move(19, 0);
        addstr("Enter hex OR \"quoted ASCII\" (16 bytes max, with or without spaces)");
        clrtoeol();
        move(20, 0);
        addstr("Press ENTER to send | T to toggle CS | BACKSPACE to delete | ESC to exit");
        clrtoeol();

        // Status bar
        move(LINES - 1, 0);
        attron(A_REVERSE);
        addstr("Ex: ABCD | AB CD | \"Hello!\" | \"Mike Wolak\" | A5 3F | Max 16 bytes");
        for (int i = 71; i < COLS; i++) addch(' ');
        standend();

        refresh();

        // Get input
        timeout(-1);
        int ch = getch();

        // Check if we're in ASCII entry mode (started with quote but not closed)
        int in_ascii_mode = 0;
        if (input_pos > 0 && input_buf[0] == '"') {
            // Check if closing quote exists
            int has_closing_quote = 0;
            for (int i = 1; i < input_pos; i++) {
                if (input_buf[i] == '"') {
                    has_closing_quote = 1;
                    break;
                }
            }
            in_ascii_mode = !has_closing_quote;
        }

        if (ch == 27) {  // ESC - exit
            break;
        }
        else if (!in_ascii_mode && (ch == 't' || ch == 'T')) {  // Toggle CS
            cs_state = !cs_state;
            SPI_CS = cs_state;
        }
        else if (ch == '\n' || ch == '\r') {  // Send command
            if (input_pos > 0) {
                int byte_count = 0;

                // Check if input is a quoted ASCII string
                if (input_buf[0] == '"') {
                    // Find closing quote
                    int end_quote = -1;
                    for (int i = 1; i < input_pos; i++) {
                        if (input_buf[i] == '"') {
                            end_quote = i;
                            break;
                        }
                    }

                    // Extract ASCII string and convert to bytes
                    if (end_quote > 0) {
                        for (int i = 1; i < end_quote && byte_count < 16; i++) {
                            tx_bytes[byte_count++] = (uint8_t)input_buf[i];
                        }
                    }
                } else {
                    // Parse hex input - flexible format:
                    // - Continuous hex: "ABCD" -> AB CD
                    // - Space-separated: "AB CD" -> AB CD
                    // - Mixed: "ABCD 12" -> AB CD 12
                    // - Single nibbles: "A" -> 0A
                    // Max 32 hex nibbles (16 bytes)

                    // First pass: extract only hex digits (skip spaces)
                    char hex_only[33] = {0};  // Max 32 nibbles + null
                    int hex_count = 0;

                    for (int i = 0; i < input_pos && hex_count < 32; i++) {
                        char c = input_buf[i];
                        if ((c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F')) {
                            hex_only[hex_count++] = c;
                        }
                        // Ignore spaces and other characters
                    }

                    // Second pass: convert pairs of hex digits to bytes
                    for (int i = 0; i < hex_count && byte_count < 16; i += 2) {
                    uint32_t val = 0;

                    // First nibble
                    char c1 = hex_only[i];
                    if (c1 >= '0' && c1 <= '9')
                        val = (c1 - '0') << 4;
                    else if (c1 >= 'a' && c1 <= 'f')
                        val = (c1 - 'a' + 10) << 4;
                    else if (c1 >= 'A' && c1 <= 'F')
                        val = (c1 - 'A' + 10) << 4;

                    // Second nibble (if present)
                    if (i + 1 < hex_count) {
                        char c2 = hex_only[i + 1];
                        if (c2 >= '0' && c2 <= '9')
                            val |= (c2 - '0');
                        else if (c2 >= 'a' && c2 <= 'f')
                            val |= (c2 - 'a' + 10);
                        else if (c2 >= 'A' && c2 <= 'F')
                            val |= (c2 - 'A' + 10);
                    }
                        // If odd number of nibbles, last byte has 0 in lower nibble

                        tx_bytes[byte_count++] = (uint8_t)val;
                    }
                }

                // Send bytes and receive responses
                if (byte_count > 0) {
                    for (int i = 0; i < byte_count; i++) {
                        rx_bytes[i] = spi_transfer(tx_bytes[i]);
                    }

                    // Format transaction for history
                    char tx_str[40] = {0};
                    char rx_str[40] = {0};
                    int tx_len = 0, rx_len = 0;

                    for (int i = 0; i < byte_count && tx_len < 38; i++) {
                        tx_len += snprintf(tx_str + tx_len, 40 - tx_len, "%02X ", tx_bytes[i]);
                    }
                    for (int i = 0; i < byte_count && rx_len < 38; i++) {
                        rx_len += snprintf(rx_str + rx_len, 40 - rx_len, "%02X ", rx_bytes[i]);
                    }

                    // Add to history
                    int hist_idx = (history_start + history_count) % MAX_HISTORY;
                    if (history_count < MAX_HISTORY) {
                        history_count++;
                    } else {
                        history_start = (history_start + 1) % MAX_HISTORY;
                    }

                    snprintf(history[hist_idx], 80, "  TX: %s-> RX: %s", tx_str, rx_str);
                }

                // Clear input
                input_buf[0] = '\0';
                input_pos = 0;
            }
        }
        else if (ch == 8 || ch == 127) {  // Backspace
            if (input_pos > 0) {
                input_pos--;
                input_buf[input_pos] = '\0';
            }
        }
        else if (ch >= ' ' && ch <= '~' && input_pos < 62) {  // Printable characters
            input_buf[input_pos++] = ch;
            input_buf[input_pos] = '\0';
        }
    }

    #undef MAX_HISTORY
}

// Main interactive UI
int main(void) {
    int selected_test = TEST_LOOPBACK;  // Start at first visible menu item (not TEST_REGISTER_DUMP)
    int old_selected_test = -1;
    int need_full_redraw = 1;
    int need_param_update = 0;
    uint32_t last_spi_irq_count = 0;
    char buf[80];

    // Initialize SPI
    spi_init(SPI_CLK_390KHZ);

    // Start in polling mode (interrupts disabled)
    use_irq_mode = 0;
    irq_disable();

    // Initialize curses
    initscr();
    noecho();
    raw();
    keypad(stdscr, TRUE);
    curs_set(0);

    // Screen layout constants
    const int menu_row = 6;
    const int result_row = menu_row + 18;  // 5 tests * 3 rows + 3 spacing

    while (1) {
        // Only full redraw on first iteration or when requested
        if (need_full_redraw) {
            clear();

            // Header
            move(0, 0);
            attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "Interactive SPI Test Suite - H:Help  I:IRQ Mode  ENTER:Run  E:Edit  Q:Quit");
            addstr(buf);
            for (int i = strlen(buf); i < COLS; i++) addch(' ');
            standend();

            // Register dump
            draw_registers(2);

            // SPI Transfer Mode (Polling vs Interrupt)
            move(5, 0);
            attron(A_REVERSE);
            addstr("[ SPI Mode ]");
            standend();
            move(5, 14);
            snprintf(buf, sizeof(buf), " %s (Press I to toggle) | IRQ Count: %u",
                     use_irq_mode ? "INTERRUPT" : "POLLING  ",
                     (unsigned int)spi_irq_count);
            addstr(buf);

            // Test menu header
            move(menu_row, 0);
            attron(A_REVERSE);
            addstr("[ Select Test ]");
            standend();

            // Results area header
            move(result_row - 1, 0);
            attron(A_REVERSE);
            addstr("[ Test Results ]");
            standend();

            // Status bar
            move(LINES - 1, 0);
            attron(A_REVERSE);
            snprintf(buf, sizeof(buf), "H:Help | I:IRQ Mode | ENTER:Run | Arrows:Nav | E:Edit | SPACE:Stop | Q:Quit");
            addstr(buf);
            for (int i = strlen(buf); i < COLS; i++) addch(' ');
            standend();

            need_full_redraw = 0;
            old_selected_test = -1;  // Force menu redraw
            need_param_update = 1;
        }

        // Update menu items only if selection changed or params changed
        if (old_selected_test != selected_test || need_param_update) {
            // Unhighlight old selection
            if (old_selected_test >= TEST_LOOPBACK) {
                move(menu_row + 2 + ((old_selected_test - TEST_LOOPBACK) * 3), 0);
                clrtoeol();
                addstr(" > ");
                if (old_selected_test == TEST_LOOPBACK) addstr("Loopback Test");
                else if (old_selected_test == TEST_SPEED_TEST) addstr("Speed Test (All Clocks)");
                else if (old_selected_test == TEST_SD_INIT) addstr("SD Card Init Pattern");
                else if (old_selected_test == TEST_MANUAL_XFER) addstr("Manual Transfer");
                else if (old_selected_test == TEST_SPI_TERMINAL) addstr("SPI Terminal (Interactive)");
            }

            // Highlight new selection
            move(menu_row + 2 + ((selected_test - TEST_LOOPBACK) * 3), 0);
            clrtoeol();
            attron(A_REVERSE);
            addstr(" > ");
            if (selected_test == TEST_LOOPBACK) addstr("Loopback Test");
            else if (selected_test == TEST_SPEED_TEST) addstr("Speed Test (All Clocks)");
            else if (selected_test == TEST_SD_INIT) addstr("SD Card Init Pattern");
            else if (selected_test == TEST_MANUAL_XFER) addstr("Manual Transfer");
            else if (selected_test == TEST_SPI_TERMINAL) addstr("SPI Terminal (Interactive)");
            standend();

            old_selected_test = selected_test;
        }

        // Update parameter lines (only for selected test or when params changed)
        if (need_param_update) {
            // Loopback params
            move(menu_row + 3, 4);
            clrtoeol();
            snprintf(buf, sizeof(buf), "Mode: %s  Iterations: %d  Transfer: %d bytes",
                     config.loopback_continuous ? "Continuous" : "Fixed",
                     config.loopback_iterations,
                     config.loopback_bytes);
            addstr(buf);

            // Speed test params
            move(menu_row + 6, 4);
            clrtoeol();
            snprintf(buf, sizeof(buf), "Mode: %s  Transfer: %d bytes",
                     config.speed_test_continuous ? "Continuous" : "Single",
                     config.speed_test_bytes);
            addstr(buf);

            // SD Init params
            move(menu_row + 9, 4);
            clrtoeol();
            addstr("Test SD card initialization sequence");

            // Manual transfer params
            move(menu_row + 12, 4);
            clrtoeol();
            addstr("Send single bytes with hex input (full screen)");

            // SPI Terminal params
            move(menu_row + 15, 4);
            clrtoeol();
            addstr("Type hex commands, see live responses (full screen)");

            need_param_update = 0;
        }

        // Update IRQ count if it changed (incremental update)
        if (spi_irq_count != last_spi_irq_count) {
            last_spi_irq_count = spi_irq_count;
            move(5, 14);
            clrtoeol();
            snprintf(buf, sizeof(buf), " %s (Press I to toggle) | IRQ Count: %u",
                     use_irq_mode ? "INTERRUPT" : "POLLING  ",
                     (unsigned int)spi_irq_count);
            addstr(buf);
        }

        refresh();

        // Get input
        timeout(-1);
        int ch = getch();

        if (ch == 'q' || ch == 'Q') {
            break;
        }
        else if (ch == 'h' || ch == 'H') {  // Help
            show_help();
            need_full_redraw = 1;  // Redraw main screen after help
        }
        else if (ch == 'i' || ch == 'I') {  // Toggle IRQ mode
            use_irq_mode = !use_irq_mode;
            if (use_irq_mode) {
                // Enable SPI interrupt (IRQ[2])
                // Mask: 0=enabled, 1=disabled
                // Enable only SPI (bit 2), mask others (bits 0,1,3-31)
                irq_setmask(~(1 << 2));  // Clear bit 2, set all others
            } else {
                // Disable all interrupts
                irq_disable();
            }
            need_full_redraw = 1;  // Redraw to update mode display
        }
        else if (ch == 65 || ch == 'k') {  // Up
            selected_test--;
            if (selected_test < TEST_LOOPBACK) selected_test = TEST_SPI_TERMINAL;
        }
        else if (ch == 66 || ch == 'j') {  // Down
            selected_test++;
            if (selected_test > TEST_SPI_TERMINAL) selected_test = TEST_LOOPBACK;
        }
        else if (ch == '\n' || ch == '\r') {  // Enter - run test
            int stop = 0;

            // Clear results area
            for (int i = 0; i < 10; i++) {
                move(result_row + i, 0);
                clrtoeol();
            }

            move(result_row, 0);
            attron(A_REVERSE);
            addstr("Running... (Press SPACE to stop)");
            standend();
            refresh();

            if (selected_test == TEST_LOOPBACK) {
                run_loopback_test(result_row + 1, &stop);
            }
            else if (selected_test == TEST_SPEED_TEST) {
                run_speed_test(result_row + 1, &stop);
            }
            else if (selected_test == TEST_SD_INIT) {
                run_sd_init_test(result_row + 1, &stop);
            }
            else if (selected_test == TEST_MANUAL_XFER) {
                run_manual_transfer();
                need_full_redraw = 1;  // Full redraw after exit
                continue;  // Skip the "test complete" message
            }
            else if (selected_test == TEST_SPI_TERMINAL) {
                run_spi_terminal();
                need_full_redraw = 1;  // Full redraw after terminal exits
                continue;  // Skip the "test complete" message
            }

            move(result_row, 0);
            clrtoeol();
            attron(A_REVERSE);
            addstr("Test complete! (Press any key to continue)");
            standend();
            refresh();

            timeout(-1);
            getch();

            // Clear "Test complete" message
            move(result_row, 0);
            clrtoeol();
            refresh();
        }
        else if (ch == 'e' || ch == 'E') {  // Edit parameters - toggle mode
            if (selected_test == TEST_LOOPBACK) {
                config.loopback_continuous = !config.loopback_continuous;
                need_param_update = 1;
            }
            else if (selected_test == TEST_SPEED_TEST) {
                config.speed_test_continuous = !config.speed_test_continuous;
                need_param_update = 1;
            }
            // Manual transfer and SPI terminal have no editable menu params
        }
        else if (ch == 67 || ch == 'l' || ch == KEY_RIGHT) {  // Right arrow - cycle transfer size forward
            if (selected_test == TEST_LOOPBACK) {
                // Cycle loopback transfer size: 2 -> 4 -> 8 -> ... -> 8192 -> 2
                if (config.loopback_bytes < 8192) {
                    config.loopback_bytes *= 2;
                } else {
                    config.loopback_bytes = 2;
                }
                need_param_update = 1;
            }
            else if (selected_test == TEST_SPEED_TEST) {
                // Cycle speed test transfer size: 2 -> 4 -> 8 -> ... -> 8192 -> 2
                if (config.speed_test_bytes < 8192) {
                    config.speed_test_bytes *= 2;
                } else {
                    config.speed_test_bytes = 2;
                }
                need_param_update = 1;
            }
        }
        else if (ch == 68 || ch == 'h' || ch == KEY_LEFT) {  // Left arrow - cycle transfer size backward
            if (selected_test == TEST_LOOPBACK) {
                // Cycle loopback transfer size: 2 <- 4 <- 8 <- ... <- 8192 <- 2
                if (config.loopback_bytes > 2) {
                    config.loopback_bytes /= 2;
                } else {
                    config.loopback_bytes = 8192;
                }
                need_param_update = 1;
            }
            else if (selected_test == TEST_SPEED_TEST) {
                // Cycle speed test transfer size: 2 <- 4 <- 8 <- ... <- 8192 <- 2
                if (config.speed_test_bytes > 2) {
                    config.speed_test_bytes /= 2;
                } else {
                    config.speed_test_bytes = 8192;
                }
                need_param_update = 1;
            }
        }
    }

    endwin();
    return 0;
}
